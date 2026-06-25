#include <avr/io.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "timerISR.h"
#include "aakba022_helper.h"
#include "aakba022_st7735.cpp"
#include "aakba022_sprites.h"
#include "usart_ATmega328p.h"

#define NUM_TASKS 6

unsigned char LR = 0;
unsigned char UD = 0;
unsigned char P  = 0;

unsigned char config_flag = 1;
unsigned char zip_arr[5]  = {0,0,0,0,0};
unsigned char saved_zip[5];
unsigned char temp_unit     = 0;  // 0=C,   1=F,     2=K
unsigned char speed_unit    = 0;  // 0=mph, 1=km/h,  2=m/s
unsigned char time_format   = 0;  // 0=24hr,1=AM/PM
unsigned char pressure_unit = 0;  // 0=Pa,  1=atm,   2=Torr

// ── Live weather data (filled by UART_Tick) ───────────────────────────────
//    Stored in whatever unit the Python bridge sends (°F, mph, hPa).
//    Display_Tick converts on the fly using the user's unit preferences.
char  wx_hi_str[8]      = "--";    // e.g. "84.2"
char  wx_hi_time[6]     = "--:--"; // "HH:MM"
char  wx_lo_str[8]      = "--";
char  wx_lo_time[6]     = "--:--";
char  wx_wind_str[8]    = "--";
char  wx_sunrise[6]     = "--:--";
char  wx_sunset[6]      = "--:--";
char  wx_rain_str[5]    = "--";    // percent, 0-100
char  wx_cloud_str[5]   = "--";
char  wx_humid_str[5]   = "--";
char  wx_press_str[10]  = "--";    // hPa raw; converted for display
unsigned char wx_valid  = 0;       // 1 once a full packet is received

// ── Forward declarations ──────────────────────────────────────────────────
enum JS_States      {JS_Read};
int JS_Tick(int);
const unsigned char JS_period = 50;

enum Display_States {Display_Rain, Display_Temp_HL, Display_Sun, Display_Misc, Display_Config};
int Display_Tick(int);
const unsigned short Display_Period = 150;

enum Config_States  {Config_Zip, Config_Temp, Config_Time, Config_Speed, Config_Pressure, Config_Done};
int Config_Tick(int);
const unsigned short Config_Period = 150;
unsigned int config_index = 0;

enum Button_States  {OnOff_Timer, OnOff_On, OnOff_Off, OnOff_Reset};
int OnOff_Tick(int);
const unsigned short Button_Period = 1000;
unsigned int Button_Timer = 0;

enum Speaker_States {Speaker_Left, Speaker_Right, Speaker_Up1, Speaker_Up2,
                     Speaker_Down1, Speaker_Down2, Idle,
                     Speaker_Press1, Speaker_Press2, Speaker_Press3};
int Speaker_Tick(int);
unsigned short Speaker_period = 150;

enum UART_States    {UART_Idle, UART_Send, UART_Receive, UART_Done};
int UART_Tick(int);
const unsigned short UART_Period = 50;  

const unsigned long GCD_PERIOD = 50;

void ADC_init() {
    ADMUX  = 0x40;
    ADCSRA = 0x87;
    ADCSRB = 0x00;
}

unsigned int ADC_read(unsigned char chnl) {
    ADMUX  = (ADMUX & 0xF0) | (chnl & 0x0F);
    ADCSRA = ADCSRA | 0x40;
    while ((ADCSRA >> 6) & 0x01) {}
    uint8_t low  = ADCL;
    uint8_t high = ADCH;
    return ((high << 8) | low);
}

void resetArduino() {
    wdt_enable(WDTO_15MS);
    while (true) {}
}

static void itoa_simple(long val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int i = 0;
    unsigned char neg = (val < 0);
    if (neg) val = -val;
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    if (neg) tmp[i++] = '-';
    int j = 0;
    for (int k = i - 1; k >= 0; k--) buf[j++] = tmp[k];
    buf[j] = '\0';
}

static unsigned char str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return (*a == '\0' && *b == '\0');
}

static void str_copy(const char* src, char* dst) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static long atoi_simple(const char* s) {
    long val = 0; unsigned char neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

const uint64_t* pick_rain_sprite() {
    if (!wx_valid) return umbrella;           
    long rain_pct  = atoi_simple(wx_rain_str);
    long cloud_pct = atoi_simple(wx_cloud_str);
    if (rain_pct  >= 50) return umbrella;      
    if (cloud_pct >= 60) return cloud;         
    return sun;                               
}

static long to_fahrenheit(long t) {
    switch (temp_unit) {
        case 0:  return t * 9 / 5 + 32;                    // C -> F
        case 2:  return (t * 10 - 2731) * 9 / 50 + 32;     // K -> F (273.1 offset, tenths)
        default: return t;                                  // already F
    }
}

static long to_mph(long w) {
    switch (speed_unit) {
        case 1:  return w * 1000 / 1609;   // km/h -> mph  (÷1.609)
        case 2:  return w * 2237 / 1000;   // m/s  -> mph  (×2.237)
        default: return w;                  // already mph
    }
}

const uint64_t* pick_temp_sprite() {
    if (!wx_valid) return sweater;
    long hi_f = to_fahrenheit(atoi_simple(wx_hi_str));
    if (hi_f < 50) return jacket;
    if (hi_f < 68) return sweater;
    return tshirt;
}

const uint64_t* pick_wind_sprite() {
    if (!wx_valid) return lo_wind;
    long wind_mph = to_mph(atoi_simple(wx_wind_str));
    return (wind_mph >= 15) ? hi_wind : lo_wind;
}

#define UART_LINE_BUF 24
static char  uart_line[UART_LINE_BUF];
static unsigned char uart_line_pos = 0;

static void uart_parse_line() {
    char* colon = uart_line;
    while (*colon && *colon != ':') colon++;
    if (*colon != ':') return;  

    *colon = '\0';            
    const char* key = uart_line;
    const char* val = colon + 1;

    if      (str_eq(key, "HI"))      { str_copy(val, wx_hi_str);   }
    else if (str_eq(key, "HITIME"))  { str_copy(val, wx_hi_time);  }
    else if (str_eq(key, "LO"))      { str_copy(val, wx_lo_str);   }
    else if (str_eq(key, "LOTIME"))  { str_copy(val, wx_lo_time);  }
    else if (str_eq(key, "WIND"))    { str_copy(val, wx_wind_str); }
    else if (str_eq(key, "SUNRISE")) { str_copy(val, wx_sunrise);  }
    else if (str_eq(key, "SUNSET"))  { str_copy(val, wx_sunset);   }
    else if (str_eq(key, "RAIN"))    { str_copy(val, wx_rain_str); }
    else if (str_eq(key, "CLOUD"))   { str_copy(val, wx_cloud_str);}
    else if (str_eq(key, "HUMID"))   { str_copy(val, wx_humid_str);}
    else if (str_eq(key, "PRESS"))   { str_copy(val, wx_press_str);}
    
}
typedef struct _task {
    signed   char  state;
    unsigned long  period;
    unsigned long  elapsedTime;
    int (*TickFct)(int);
} task;

task tasks[NUM_TASKS];

void TimerISR() {
    for (unsigned int i = 0; i < NUM_TASKS; i++) {
        if (tasks[i].elapsedTime == tasks[i].period) {
            tasks[i].state = tasks[i].TickFct(tasks[i].state);
            tasks[i].elapsedTime = 0;
        }
        tasks[i].elapsedTime += GCD_PERIOD;
    }
}

int JS_Tick(int j) {
    switch (j) {
        case JS_Read:
        if (PINC & 0x04) {
            P  = 0;
            unsigned int x = ADC_read(0);
            unsigned int y = ADC_read(1);
            UD = map_value(20, 800, 0, 2, y);
            LR = map_value(20, 800, 0, 2, x);
        } else { P = 1; LR = 1; UD = 1; }
        break;
    }
    switch(j){case JS_Read:break;}
    return j;
}

int Display_Tick(int d) {
    static int prev_d = -1;
    switch (d) {
        case Display_Config:
        if (config_flag == 0) { d = Display_Rain; }
        break;

        case Display_Rain:
        if (LR == 2) { d = Display_Temp_HL; }
        else if (LR == 0) { d = Display_Misc; }
        break;

        case Display_Temp_HL:
        if (LR == 2) { d = Display_Sun; }
        else if (LR == 0) { d = Display_Rain; }
        break;

        case Display_Sun:
        if (LR == 2) { d = Display_Misc; }
        else if (LR == 0) { d = Display_Temp_HL; }
        break;

        case Display_Misc:
        if (LR == 2) { d = Display_Rain; }
        else if (LR == 0) { d = Display_Sun; }
        break;
    }

    if (d == prev_d && d != Display_Config) { return d; }
    prev_d = d;

    switch (d) {
        case Display_Config:
        break;

        case Display_Rain:
        st7735_fill_screen(BLACK);
        st7735_draw_sprite64(0, 32, pick_rain_sprite(), WHITE, BLACK);
        st7735_draw_string(64, 10, "Rain %:", WHITE, BLACK, 1);
        st7735_draw_string(64, 18, wx_rain_str, WHITE, BLACK, 1);
        st7735_draw_string(64, 74, "Clouds %:", WHITE, BLACK, 1);
        st7735_draw_string(64, 82, wx_cloud_str, WHITE, BLACK, 1);
        break;

        case Display_Temp_HL: {
        char hi_disp[10], lo_disp[10];
        if (temp_unit == 0) {
            long hi_c = ((long)(wx_hi_str[0] ? atoi_simple(wx_hi_str) : 0) - 32) * 5 / 9;
            long lo_c = ((long)(wx_lo_str[0] ? atoi_simple(wx_lo_str) : 0) - 32) * 5 / 9;
            itoa_simple(hi_c, hi_disp);
            itoa_simple(lo_c, lo_disp);
        } 
        else if (temp_unit == 1) {
            str_copy(wx_hi_str, hi_disp);
            str_copy(wx_lo_str, lo_disp);
        } 
        else {
            long hi_k = ((long)(atoi_simple(wx_hi_str)) - 32) * 5 / 9 + 273;
            long lo_k = ((long)(atoi_simple(wx_lo_str)) - 32) * 5 / 9 + 273;
            itoa_simple(hi_k, hi_disp);
            itoa_simple(lo_k, lo_disp);
        }

        st7735_fill_screen(BLACK);
        st7735_draw_sprite64(0, 32, pick_temp_sprite(), WHITE, BLACK);
        st7735_draw_string(64, 10, "Temp High:",  WHITE, BLACK, 1);
        st7735_draw_string(64, 18, hi_disp,       WHITE, BLACK, 1);
        st7735_draw_string(64, 26, wx_hi_time,    WHITE, BLACK, 1);
        st7735_draw_string(64, 74, "Temp Low:",   WHITE, BLACK, 1);
        st7735_draw_string(64, 82, lo_disp,       WHITE, BLACK, 1);
        st7735_draw_string(64, 91, wx_lo_time,    WHITE, BLACK, 1);
        break;
        }

        case Display_Sun:
        st7735_fill_screen(BLACK);
        st7735_draw_sprite64(0,  0, sun,  WHITE, BLACK);
        st7735_draw_string(64, 10, "Sunrise:",  WHITE, BLACK, 1);
        st7735_draw_string(64, 18, wx_sunrise,  WHITE, BLACK, 1);
        st7735_draw_sprite64(0, 64, moon, WHITE, BLACK);
        st7735_draw_string(64, 74, "Sunset:",   WHITE, BLACK, 1);
        st7735_draw_string(64, 82, wx_sunset,   WHITE, BLACK, 1);
        break;

        case Display_Misc: {
        char wind_disp[10];
        long wind_mph = atoi_simple(wx_wind_str);
        if (speed_unit == 0) {
            str_copy(wx_wind_str, wind_disp);         
        } else if (speed_unit == 1) {
            itoa_simple(wind_mph * 16 / 10, wind_disp); 
        } else {
            itoa_simple(wind_mph * 44 / 100, wind_disp);
        }

        char press_disp[12];
        long press_hpa = atoi_simple(wx_press_str);
        if (pressure_unit == 0) {
            itoa_simple(press_hpa * 100, press_disp);
        } 
        else if (pressure_unit == 1) {
            itoa_simple(press_hpa / 1013, press_disp);
        } 
        else {
            itoa_simple(press_hpa * 3 / 4, press_disp);
        }

        st7735_fill_screen(BLACK);
        st7735_draw_sprite64(0, 32, pick_wind_sprite(), WHITE, BLACK);
        st7735_draw_string(64, 10, "Humidity:",   WHITE, BLACK, 1);
        st7735_draw_string(64, 18, wx_humid_str,  WHITE, BLACK, 1);
        st7735_draw_string(64, 36, "Pressure:",   WHITE, BLACK, 1);
        st7735_draw_string(64, 44, press_disp,    WHITE, BLACK, 1);
        st7735_draw_string(64, 62, "Wind Speed:", WHITE, BLACK, 1);
        st7735_draw_string(64, 70, wind_disp,     WHITE, BLACK, 1);
        break;
        }
    }

    return d;
}

int Config_Tick(int c) {
    static int prev_index           = -1;
    static unsigned char prev_zip[5] = {255,255,255,255,255};
    static int prev_c               = -1;
    static unsigned char prev_P     = 0;
    unsigned char P_pressed = (P == 1 && prev_P == 0);
    prev_P = P;

    switch (c) {
        case Config_Zip:
        if (P_pressed) {
            for (int zipDummy = 0; zipDummy < 5; zipDummy++)
                saved_zip[zipDummy] = zip_arr[zipDummy];
            c = Config_Temp; config_index = 0;
        }
        break;

        case Config_Temp:
        if (P_pressed) { temp_unit = config_index; c = Config_Speed; config_index = 0; }
        break;

        case Config_Speed:
        if (P_pressed) { speed_unit = config_index; c = Config_Time; config_index = 0; }
        break;

        case Config_Time:
        if (P_pressed) { time_format = config_index; c = Config_Pressure; config_index = 0; }
        break;

        case Config_Pressure:
        if (P_pressed) { pressure_unit = config_index; c = Config_Done; config_index = 0; config_flag = 0; }
        break;

        case Config_Done:
        break;
    }

    switch (c) {
        case Config_Zip: {
        if (LR == 2 && config_index < 4) config_index++;
        if (LR == 0 && config_index > 0) config_index--;
        if (UD == 0 && zip_arr[config_index] < 9) zip_arr[config_index]++;
        if (UD == 2 && zip_arr[config_index] > 0) zip_arr[config_index]--;
        unsigned char zip_changed = 0;
        for (int i = 0; i < 5; i++) {
            if (zip_arr[i] != prev_zip[i]) { zip_changed = 1; break; }
        }
        unsigned char needs_redraw = (c != prev_c) || (config_index != prev_index) || zip_changed;
        if (needs_redraw) {
            st7735_fill_screen(BLACK);
            st7735_draw_string(20, 45, "Enter ZIP:", WHITE, BLACK, 1);
            for (int i = 0; i < 5; i++) {
                unsigned short fg = (i == (int)config_index) ? RED : WHITE;
                st7735_draw_char(i * 10 + 40, 60, zip_arr[i] + '0', fg, BLACK, 1);
            }
            prev_index = config_index;
            for (int i = 0; i < 5; i++) prev_zip[i] = zip_arr[i];
        }
        prev_c = c;
        break;
        }

        case Config_Temp: {
        if (LR == 2 && config_index < 2) config_index++;
        if (LR == 0 && config_index > 0) config_index--;
        unsigned char needs_redraw_temp = (c != prev_c) || (config_index != prev_index);
        if (needs_redraw_temp) {
            st7735_fill_screen(BLACK);
            st7735_draw_string(10, 45, "Temp Unit:", WHITE, BLACK, 1);
            const char* units[3] = {"C", "F", "K"};
            for (unsigned char i = 0; i < 3; i++) {
                unsigned short fg = (i == config_index) ? RED : WHITE;
                st7735_draw_string(i * 20 + 30, 60, units[i], fg, BLACK, 1);
            }
            prev_index = config_index;
        }
        prev_c = c;
        break;
        }

        case Config_Speed: {
        if (LR == 2 && config_index < 2) config_index++;
        if (LR == 0 && config_index > 0) config_index--;
        unsigned char needs_redraw_speed = (c != prev_c) || (config_index != prev_index);
        if (needs_redraw_speed) {
            st7735_fill_screen(BLACK);
            st7735_draw_string(5, 45, "Wind Speed:", WHITE, BLACK, 1);
            const char* units[3] = {"mph", "km/h", "m/s"};
            for (int i = 0; i < 3; i++) {
                unsigned short fg = (i == (int)config_index) ? RED : WHITE;
                st7735_draw_string(i * 35 + 10, 60, units[i], fg, BLACK, 1);
            }
            prev_index = config_index;
        }
        prev_c = c;
        break;
        }

        case Config_Time: {
        if (LR == 2 && config_index < 1) config_index++;
        if (LR == 0 && config_index > 0) config_index--;
        unsigned char needs_redraw_time = (c != prev_c) || (config_index != prev_index);
        if (needs_redraw_time) {
            st7735_fill_screen(BLACK);
            st7735_draw_string(5, 45, "Time Format:", WHITE, BLACK, 1);
            const char* units[2] = {"24hr", "AM/PM"};
            for (int i = 0; i < 2; i++) {
                unsigned short fg = (i == (int)config_index) ? RED : WHITE;
                st7735_draw_string(i * 40 + 25, 60, units[i], fg, BLACK, 1);
            }
            prev_index = config_index;
        }
        prev_c = c;
        break;
        }

        case Config_Pressure: {
        if (LR == 2 && config_index < 2) config_index++;
        if (LR == 0 && config_index > 0) config_index--;
        unsigned char needs_redraw_pressure = (c != prev_c) || (config_index != prev_index);
        if (needs_redraw_pressure) {
            st7735_fill_screen(BLACK);
            st7735_draw_string(5, 45, "Pressure:", WHITE, BLACK, 1);
            const char* units[3] = {"Pa", "atm", "Torr"};
            for (int i = 0; i < 3; i++) {
                unsigned short fg = (i == (int)config_index) ? RED : WHITE;
                st7735_draw_string(i * 35 + 10, 60, units[i], fg, BLACK, 1);
            }
            prev_index = config_index;
        }
        prev_c = c;
        break;
        }
    }

    return c;
}

int OnOff_Tick(int o) {
    static unsigned char prev_o = OnOff_On;

    switch (o) {
        case OnOff_Timer:
        if (P == 1) {
            Button_Timer++;
            if (Button_Timer > 8) { o = OnOff_Reset; }
        } else {
            if (Button_Timer > 4) {
                if (prev_o == OnOff_On)       o = OnOff_Off;
                else if (prev_o == OnOff_Off) o = OnOff_On;
            }
            Button_Timer = 0;
        }
        break;

        case OnOff_On:  o = OnOff_Timer; prev_o = OnOff_On;  break;
        case OnOff_Off: o = OnOff_Timer; prev_o = OnOff_Off; break;
    }

    switch (o) {
        case OnOff_On:    st7735_display_on();  break;
        case OnOff_Off:   st7735_display_off(); break;
        case OnOff_Reset: resetArduino();       break;
        case OnOff_Timer: break;
    }

    return o;
}

int Speaker_Tick(int speakerstate) {
    static unsigned int note_timer = 0;

    switch (speakerstate) {
        case Idle:
        if (LR == 2)                 { speakerstate = Speaker_Right;  note_timer = 0; TCCR0A |= (1 << COM0A0); }
        if (LR == 0)                 { speakerstate = Speaker_Left;   note_timer = 0; TCCR0A |= (1 << COM0A0); }
        if (UD == 0 && config_flag)  { speakerstate = Speaker_Up1;    note_timer = 0; TCCR0A |= (1 << COM0A0); }
        if (UD == 2 && config_flag)  { speakerstate = Speaker_Down1;  note_timer = 0; TCCR0A |= (1 << COM0A0); }
        if (P  == 1)                 { speakerstate = Speaker_Press1; note_timer = 0; TCCR0A |= (1 << COM0A0); }
        break;

        case Speaker_Up1:    note_timer++; if (note_timer >= (30  / Speaker_period)) { speakerstate = Speaker_Up2;    note_timer = 0; } break;
        case Speaker_Up2:    note_timer++; if (note_timer >= (120 / Speaker_period)) { speakerstate = Idle; }          break;
        case Speaker_Down1:  note_timer++; if (note_timer >= (30  / Speaker_period)) { speakerstate = Speaker_Down2;  note_timer = 0; } break;
        case Speaker_Down2:  note_timer++; if (note_timer >= (120 / Speaker_period)) { speakerstate = Idle; }          break;
        case Speaker_Left:   note_timer++; if (note_timer >= (12  / Speaker_period)) { speakerstate = Idle; note_timer = 0; } break;
        case Speaker_Right:  note_timer++; if (note_timer >= (12  / Speaker_period)) { speakerstate = Idle; note_timer = 0; } break;
        case Speaker_Press1: note_timer++; if (note_timer >= (30  / Speaker_period)) { speakerstate = Speaker_Press2; note_timer = 0; } break;
        case Speaker_Press2: note_timer++; if (note_timer >= (30  / Speaker_period)) { speakerstate = Speaker_Press3; note_timer = 0; } break;
        case Speaker_Press3: note_timer++; if (note_timer >= (30  / Speaker_period)) { note_timer = 0; speakerstate = Idle; }          break;
    }

    switch (speakerstate) {
        case Speaker_Up1:    OCR0A = 255; TCCR0A |= (1 << COM0A0); break;
        case Speaker_Up2:    OCR0A = 225; TCCR0A |= (1 << COM0A0); break;
        case Speaker_Down1:  OCR0A = 225; TCCR0A |= (1 << COM0A0); break;
        case Speaker_Down2:  OCR0A = 255; TCCR0A |= (1 << COM0A0); break;
        case Speaker_Left:   OCR0A = 225; TCCR0A |= (1 << COM0A0); break;
        case Speaker_Right:  OCR0A = 255; TCCR0A |= (1 << COM0A0); break;
        case Speaker_Press1: OCR0A = 255; TCCR0A |= (1 << COM0A0); break;
        case Speaker_Press2: OCR0A = 240; TCCR0A |= (1 << COM0A0); break;
        case Speaker_Press3: OCR0A = 225; TCCR0A |= (1 << COM0A0); break;
        case Idle:           TCCR0A &= ~(1 << COM0A0);              break;
    }

    return speakerstate;
}

int UART_Tick(int u) {
    static unsigned char send_idx  = 0;  
    static char          zip_buf[8];     
    static unsigned char zip_buf_len = 0;

    switch (u) {
        case UART_Idle:
        if (config_flag == 0) {
            for (int i = 0; i < 5; i++) zip_buf[i] = saved_zip[i] + '0';
            zip_buf[5] = '\r'; zip_buf[6] = '\n'; zip_buf[7] = '\0';
            zip_buf_len = 7;
            send_idx = 0;
            uart_line_pos = 0;
            u = UART_Send;
        }
        break;

        case UART_Send:
        if (send_idx < zip_buf_len) {
            if (USART_IsSendReady()) { USART_Send(zip_buf[send_idx++]); }
        } else {
            u = UART_Receive; 
        }
        break;

        case UART_Receive:
            while (USART_HasReceived()) {
            char ch = (char)USART_Receive();
            if (ch == '\n') {
                uart_line[uart_line_pos] = '\0';
                uart_line_pos = 0;
                if (str_eq(uart_line, "END")) {
                    wx_valid = 1;
                    u = UART_Done;
                    break;
                } else if (uart_line[0]=='E' && uart_line[1]=='R' && uart_line[2]=='R') {u = UART_Idle;break;} 
                else {uart_parse_line();}
            } 
            else if (ch != '\r') {                 
                if (uart_line_pos < UART_LINE_BUF - 1)
                    uart_line[uart_line_pos++] = ch;
            }
            }
        break;

        case UART_Done:
        break;
    }

    return u;
}

int main() {
    PORTC = 0xFF; DDRC = 0x00;
    PORTD = 0x00; DDRD = 0xFF;
    PORTB = 0x00; DDRB = 0xFF;

    st7735_init();
    st7735_fill_screen(BLACK);

    ADC_init();
    initUSART();

    OCR0A  = 128;
    TCCR0A = (1 << COM0A0) | (1 << WGM01);
    TCCR0B = (TCCR0B & 0xF8) | 0x03;

    tasks[0].period      = JS_period;
    tasks[0].state       = JS_Read;
    tasks[0].elapsedTime = JS_period;
    tasks[0].TickFct     = &JS_Tick;

    tasks[1].period      = Display_Period;
    tasks[1].state       = Display_Config;
    tasks[1].elapsedTime = Display_Period;
    tasks[1].TickFct     = &Display_Tick;

    tasks[2].period      = Config_Period;
    tasks[2].state       = Config_Zip;
    tasks[2].elapsedTime = Config_Period;
    tasks[2].TickFct     = &Config_Tick;

    tasks[3].period      = Button_Period;
    tasks[3].state       = OnOff_Timer;
    tasks[3].elapsedTime = Button_Period;
    tasks[3].TickFct     = &OnOff_Tick;

    tasks[4].period      = Speaker_period;
    tasks[4].state       = Idle;
    tasks[4].elapsedTime = Speaker_period;
    tasks[4].TickFct     = &Speaker_Tick;

    tasks[5].period      = UART_Period;   
    tasks[5].state       = UART_Idle;
    tasks[5].elapsedTime = UART_Period;
    tasks[5].TickFct     = &UART_Tick;

    TimerSet(GCD_PERIOD);
    TimerOn();

    while (1) {}
}