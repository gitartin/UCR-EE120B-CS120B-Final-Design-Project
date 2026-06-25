#include <avr/pgmspace.h>
#include <avr/io.h>
#include <util/delay.h>
//some guy on github did something similar and it made writing the code a lot easier to use these keywords
#define ST77XX_SWRESET 0x01
#define ST77XX_SLPOUT 0x11
#define ST77XX_NORON 0x13
#define ST77XX_INVOFF 0x20
#define ST77XX_DISPON 0x29
#define ST77XX_CASET 0x2A
#define ST77XX_RASET 0x2B
#define ST77XX_RAMWR 0x2C
#define ST77XX_MADCTL 0x36
#define ST77XX_COLMOD  0x3A
#define ST77XX_FRMCTR1 0xB1
#define ST77XX_FRMCTR2 0xB2
#define ST77XX_FRMCTR3 0xB3
#define ST77XX_INVCTR 0xB4
#define ST77XX_PWCTR1 0xC0
#define ST77XX_PWCTR2 0xC1
#define ST77XX_PWCTR3 0xC2
#define ST77XX_PWCTR4 0xC3
#define ST77XX_PWCTR5 0xC4
#define ST77XX_VMCTR1 0xC5
#define ST77XX_GMCTRP1 0xE0
#define ST77XX_GMCTRN1 0xE1

#define ST7735_CS_DDR DDRB
#define ST7735_CS_PORT PORTB
#define ST7735_CS_PIN PB2

#define ST7735_DC_DDR DDRB
#define ST7735_DC_PORT PORTB
#define ST7735_DC_PIN PB1

#define ST7735_RST_DDR DDRB
#define ST7735_RST_PORT PORTB
#define ST7735_RST_PIN PB0

#define ST7735_WIDTH 128
#define ST7735_HEIGHT 128
#define ST7735_COL_OFFSET 2
#define ST7735_ROW_OFFSET 3

#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0x001F

//the arduino just wouldn't run correctly without this progmem stuff? it kept displaying random colored pixels. claude suggested to use PROGMEM and pgmspace library 
//because it suspected the matrix below was using too much memory and it started working just fine afterwards.
static const unsigned char font5x7[][5] PROGMEM = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x08,0x2A,0x1C,0x2A,0x08}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x00,0x08,0x14,0x22,0x41}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x41,0x22,0x14,0x08,0x00}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x04,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\'
    {0x00,0x41,0x41,0x7F,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
    {0x00,0x01,0x02,0x04,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 'f'
    {0x08,0x14,0x54,0x54,0x3C}, // 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 'z'
    {0x00,0x08,0x36,0x41,0x00}, // '{'
    {0x00,0x00,0x7F,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00}, // '}'
    {0x08,0x08,0x2A,0x1C,0x08}, // '~'
};

#define ST7735_BL_DDR  DDRB
#define ST7735_BL_PORT PORTB
#define ST7735_BL_PIN  PB4

void spi_init() {
    DDRB |= (1 << PB3) | (1 << PB5);

    ST7735_CS_DDR  |= (1 << ST7735_CS_PIN);
    ST7735_DC_DDR  |= (1 << ST7735_DC_PIN);
    ST7735_RST_DDR |= (1 << ST7735_RST_PIN);
    ST7735_BL_DDR  |= (1 << ST7735_BL_PIN);  

    ST7735_CS_PORT  |= (1 << ST7735_CS_PIN);
    ST7735_RST_PORT |= (1 << ST7735_RST_PIN);
    ST7735_BL_PORT  |= (1 << ST7735_BL_PIN); 
    SPCR = (1 << SPE) | (1 << MSTR);
}

void st7735_display_on()  { ST7735_BL_PORT |=  (1 << ST7735_BL_PIN); }
void st7735_display_off() { ST7735_BL_PORT &= ~(1 << ST7735_BL_PIN); }

void spi_send(unsigned char data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF)));
}

static void write_cmd(unsigned char cmd) {
    ST7735_DC_PORT  &= ~(1 << ST7735_DC_PIN);   
    ST7735_CS_PORT  &= ~(1 << ST7735_CS_PIN);
    spi_send(cmd);
    ST7735_CS_PORT  |=  (1 << ST7735_CS_PIN);
}

static void write_data8(unsigned char data) {
    ST7735_DC_PORT  |=  (1 << ST7735_DC_PIN);
    ST7735_CS_PORT  &= ~(1 << ST7735_CS_PIN);    
    spi_send(data);
    ST7735_CS_PORT  |=  (1 << ST7735_CS_PIN);
}

static void write_data16(unsigned short data) {
    ST7735_DC_PORT  |=  (1 << ST7735_DC_PIN);
    ST7735_CS_PORT  &= ~(1 << ST7735_CS_PIN);    
    spi_send(data >> 8);
    spi_send(data & 0xFF);
    ST7735_CS_PORT  |=  (1 << ST7735_CS_PIN);
}

static void set_addr_window(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1) {
    write_cmd(ST77XX_CASET);
    ST7735_DC_PORT  |=  (1 << ST7735_DC_PIN); 
    ST7735_CS_PORT  &= ~(1 << ST7735_CS_PIN);
    spi_send(0x00);
    spi_send(x0 + ST7735_COL_OFFSET);
    spi_send(0x00);
    spi_send(x1 + ST7735_COL_OFFSET);
    ST7735_CS_PORT  |=  (1 << ST7735_CS_PIN);

    write_cmd(ST77XX_RASET);
    ST7735_DC_PORT  |=  (1 << ST7735_DC_PIN); 
    ST7735_CS_PORT  &= ~(1 << ST7735_CS_PIN);
    spi_send(0x00);
    spi_send(y0 + ST7735_ROW_OFFSET);
    spi_send(0x00);
    spi_send(y1 + ST7735_ROW_OFFSET);
    ST7735_CS_PORT  |=  (1 << ST7735_CS_PIN);

    write_cmd(ST77XX_RAMWR);
}

void st7735_init(void) {
    spi_init();

    ST7735_RST_PORT |=  (1 << ST7735_RST_PIN); 
    _delay_ms(10);
    ST7735_RST_PORT &= ~(1 << ST7735_RST_PIN);  
    _delay_ms(10);
    ST7735_RST_PORT |=  (1 << ST7735_RST_PIN); 
    _delay_ms(120);

    write_cmd(ST77XX_SWRESET); 
    _delay_ms(150);
    write_cmd(ST77XX_SLPOUT);  
    _delay_ms(120);

    write_cmd(ST77XX_FRMCTR1);
    write_data8(0x01); 
    write_data8(0x2C); 
    write_data8(0x2D);

    write_cmd(ST77XX_FRMCTR2);
    write_data8(0x01); 
    write_data8(0x2C); 
    write_data8(0x2D);

    write_cmd(ST77XX_FRMCTR3);
    write_data8(0x01); 
    write_data8(0x2C); 
    write_data8(0x2D);
    write_data8(0x01); 
    write_data8(0x2C); 
    write_data8(0x2D);

    write_cmd(ST77XX_INVCTR);  // Column inversion
    write_data8(0x07);

    // Power control
    write_cmd(ST77XX_PWCTR1);
    write_data8(0xA2); 
    write_data8(0x02); 
    write_data8(0x84);

    write_cmd(ST77XX_PWCTR2);
    write_data8(0xC5);

    write_cmd(ST77XX_PWCTR3);
    write_data8(0x0A); 
    write_data8(0x00);

    write_cmd(ST77XX_PWCTR4);
    write_data8(0x8A); 
    write_data8(0x2A);

    write_cmd(ST77XX_PWCTR5);
    write_data8(0x8A); 
    write_data8(0xEE);

    write_cmd(ST77XX_VMCTR1); 
    write_data8(0x0E);

    write_cmd(ST77XX_MADCTL);
    write_data8(0xC0);

    write_cmd(ST77XX_COLMOD);
    write_data8(0x05);

    // Gamma correction (positive + negative)
    write_cmd(ST77XX_GMCTRP1);
    write_data8(0x0F); write_data8(0x1A); write_data8(0x0F);
    write_data8(0x18); write_data8(0x2F); write_data8(0x28);
    write_data8(0x20); write_data8(0x22); write_data8(0x1F);
    write_data8(0x1B); write_data8(0x23); write_data8(0x37);
    write_data8(0x00); write_data8(0x07); write_data8(0x02);
    write_data8(0x10);

    write_cmd(ST77XX_GMCTRN1);
    write_data8(0x0F); write_data8(0x1B); write_data8(0x0F);
    write_data8(0x17); write_data8(0x33); write_data8(0x2C);
    write_data8(0x29); write_data8(0x2E); write_data8(0x30);
    write_data8(0x30); write_data8(0x39); write_data8(0x3F);
    write_data8(0x00); write_data8(0x07); write_data8(0x03);
    write_data8(0x10);

    write_cmd(ST77XX_NORON);  _delay_ms(10);
    write_cmd(ST77XX_DISPON); _delay_ms(100);
}

void st7735_draw_pixel(unsigned char x, unsigned char y, unsigned short color) {
    if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) return;
    set_addr_window(x, y, x, y);
    write_data16(color);
}

void st7735_fill_rect(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned short color) {
    if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) {return;}
    if (x + w > ST7735_WIDTH) {w = ST7735_WIDTH  - x;}
    if (y + h > ST7735_HEIGHT) {h = ST7735_HEIGHT - y;}

    set_addr_window(x, y, x + w - 1, y + h - 1);

    unsigned char hi = color >> 8;
    unsigned lo = color & 0xFF;
    unsigned int count = w * h;

    ST7735_DC_PORT |= (1 << ST7735_DC_PIN);
    ST7735_CS_PORT &= ~(1 << ST7735_CS_PIN);
    while (count--) {spi_send(hi); spi_send(lo);}
    ST7735_CS_PORT |= (1 << ST7735_CS_PIN);
}

void st7735_fill_screen(unsigned short color) {
    st7735_fill_rect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

void st7735_draw_hline(unsigned char x, unsigned char y, unsigned char len, unsigned short color) {
    st7735_fill_rect(x, y, len, 1, color);
}

void st7735_draw_vline(unsigned char x, unsigned char y, unsigned char len, unsigned short colour) {
    st7735_fill_rect(x, y, 1, len, colour);
}

void st7735_draw_rect(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned short color) {
    st7735_draw_hline(x, y, w, color); 
    st7735_draw_hline(x, y + h - 1, w, color); 
    st7735_draw_vline(x, y, h, color); 
    st7735_draw_vline(x + w - 1, y, h, color); 
}

void st7735_draw_char(unsigned char x, unsigned char y, unsigned char c, unsigned short fg, unsigned short bg, unsigned char scale) {
    if (c < 0x20 || c > 0x7E) {c = '?';}

    unsigned char idx = (unsigned char)(c - 0x20);
    unsigned char fg_hi = fg >> 8, fg_lo = fg & 0xFF;
    unsigned char bg_hi = bg >> 8, bg_lo = bg & 0xFF;

    for (unsigned char col = 0; col < 5; col++) {
        unsigned char col_bits = pgm_read_byte(&font5x7[idx][col]);

        for (unsigned char row = 0; row < 7; row++) {
            unsigned char px = (col_bits >> row) & 0x01;
            unsigned char draw_x = x + col * scale;
            unsigned char draw_y = y + row * scale;

            if (scale == 1) {
                set_addr_window(draw_x, draw_y, draw_x, draw_y);
                ST7735_DC_PORT |= (1 << ST7735_DC_PIN); 
                ST7735_CS_PORT &= ~(1 << ST7735_CS_PIN);
                if (px) {spi_send(fg_hi); spi_send(fg_lo);}
                else {spi_send(bg_hi); spi_send(bg_lo);}
                ST7735_CS_PORT |= (1 << ST7735_CS_PIN);
            } 
            else {st7735_fill_rect(draw_x, draw_y, scale, scale, (px ? fg : bg));}
        }
    }
    st7735_fill_rect(x + 5 * scale, y, scale, 7 * scale, bg);
}

void st7735_draw_string(unsigned char x, unsigned char y, const char *str, unsigned short fg, unsigned short bg, unsigned char scale) {
    unsigned char cur_x = x;
    unsigned char char_w = (5 + 1) * scale; 
    unsigned char char_h = 7 * scale;
    while (*str) {
        if (*str == '\n' || cur_x + char_w > ST7735_WIDTH) {cur_x = x; y += char_h + scale; if (*str == '\n') {str++; continue;}}
        st7735_draw_char(cur_x, y, *str, fg, bg, scale);
        cur_x += char_w;
        str++;
    }
}

uint64_t pgm_read_u64(const uint64_t *addr) {
    const uint32_t *ptr = (const uint32_t *)addr;

    uint32_t lo = pgm_read_dword(&ptr[0]);
    uint32_t hi = pgm_read_dword(&ptr[1]);

    return ((uint64_t)hi << 32) | lo;
}

void st7735_draw_sprite64(unsigned char x,unsigned char y,const uint64_t *sprite,unsigned short fg, unsigned short bg) {
    for (unsigned char row = 0; row < 64; row++) {
        uint64_t bits = pgm_read_u64(&sprite[row]);
        for (unsigned char col = 0; col < 64; col++) {
            unsigned short color = (bits & (0x8000000000000000ULL >> col)) ? fg : bg;
            st7735_draw_pixel(x + col, y + row, color);
        }
    }
}