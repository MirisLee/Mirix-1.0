/*
 * Mirix 1.0/kernel/chr_dev/console.c
 * (C) 2022 Miris Lee
 */

#include <mirix/sched.h>
#include <mirix/tty.h>
#include <asm/io.h>
#include <asm/system.h>

#define ORIG_X              (*(unsigned char *)0x90000)
#define ORIG_Y              (*(unsigned char *)0x90001)
#define ORIG_VIDEO_PAGE     (*(unsigned char *)0x90004)
#define ORIG_VIDEO_MODE     ((*(unsigned short *)0x90006) & 0xff)
#define ORIG_VIDEO_COLS     ((*(unsigned short *)0x90006) >> 8)
#define ORIG_VIDEO_LINES    (25)
#define ORIG_VIDEO_EGA_AX   (*(unsigned short *)0x90008)
#define ORIG_VIDEO_EGA_BX   (*(unsigned short *)0x9000a)
#define ORIG_VIDEO_EGA_CX   (*(unsigned short *)0x9000c)

#define VIDEO_MDA   0x10    /* monochrome text display */
#define VIDEO_CGA   0x11    /* CGA display */
#define VIDEO_EGAM  0x20    /* EGA/VGA monochrome */
#define VIDEO_EGAC  0x21    /* EGA/VGA color */

#define NPAR 16

extern void keyboard_int(void);

static unsigned char    video_type;
static unsigned long    video_cols;
static unsigned long    video_row_size;
static unsigned long    video_lines;
static unsigned char    video_page;
static unsigned long    video_mem_start, video_mem_end;
static unsigned short   video_reg_port, video_val_port;
static unsigned short   video_erase;
static unsigned long    scr_start, scr_end;
static unsigned long    pos, x, y, top, bottom;
static unsigned long    state = 0, npar, par[NPAR]; /* ANSI escape queue */
static unsigned long    ques = 0;
static unsigned char    attr = 0x07;    /* white words on black background */

static void beep(void);

#define RESPONSE    "\033[?1;2c"    /* control sequence introducer */

static inline void gotoxy(unsigned int dest_x, unsigned int dest_y) {
    if (dest_x > video_cols || dest_y >= video_rows) return;
    x = dest_x;
    y = dest_y;
    pos = scr_start + y * video_row_size + (x << 1);
}

static inline void set_scr_start(void) {
    cli();
    outb_p(12, video_reg_port);
    outb_p(((scr_start - video_mem_start) >> 9) & 0xff, video_val_port);
    outb_p(13, video_reg_port);
    outb_p(((scr_start - video_mem_start) >> 1) & 0xff, video_val_port);
    sti();
}

/* scroll up a line (screen move down) */
static void scrup(void) {
    if (video_type == VIDEO_EGAC || video_type == VIDEO_EGAM) {
        /* scroll the whole screen */
        if (top == 0 && bottom == video_lines) {
            scr_start += video_row_size;
            pos += video_row_size;
            scr_end += video_row_size;
            if (scr_end > video_mem_end) {
                __asm__(
                    "cld; rep; movsl\n\t"
                    "movl _video_cols, %%ecx\n\t"
                    "rep; stosw"
                    :: "a" (video_erase), 
                    "c" ((video_lines - 1) * video_cols >> 1),
                    "D" (video_mem_start), "S" (scr_start)
                    : "cx", "di", "si"
                );
                scr_end -= scr_start - video_mem_start;
                pos -= scr_start - video_mem_start;
                scr_start -= video_mem_start;
            } else {
                __asm__(
                    "cld; rep; stosw"
                    :: "a" (video_erase), "c" (video_cols), 
                    "D" (scr_end - video_row_size)
                    : "cx", "di"
                );
            }
            set_scr_start();
        } else {
            __asm__(
                "cld; rep; movsl\n\t"
                "movl _video_cols, %%ecx\n\t"
                "rep; stosw"
                :: "a" (video_erase), 
                "c" ((bottom - top - 1) * video_cols >> 1), 
                "D" (scr_start + top * video_row_size), 
                "S" (scr_start + (top + 1) * video_row_size)
                : "cx", "di", "si"
            );
        }
    } else {    /* not EGA/VGA */
        __asm__(
            "cld; rep; movsl\n\t"
            "movl _video_cols, %%ecx\n\t"
            "rep; stosw"
            :: "a" (video_erase), 
            "c" ((bottom - top - 1) * video_cols >> 1), 
            "D" (scr_start + top * video_row_size), 
            "S" (scr_start + (top + 1) * video_row_size)
            : "cx", "di", "si"
        );
    }
}

/* scroll down a line (screen move up) */
static void scrdown(void) {
    __asm__(
        "std; rep; movsl\n\t"
        "addl $2, %%edi\n\t"
        "movl _video_cols, %%ecx\n\t"
        "rep; stosw"
        :: "a" (video_erase),
        "c" ((bottom - top - 1) * video_cols >> 1),
        "D" (scr_start + bottom * video_row_size - 4),
        "S" (scr_start + (bottom - 1) * video_row_size - 4)
        : "ax", "cx", "di", "si"
    );
}

/* line feed */
static void lf(void) {
    if (y < bottom - 1) {
        y++;
        pos += video_row_size;
        return;
    }
    scrup();
}

/* reverse line feed */
static void ri(void) {
    if (y > top) {
        y--;
        pos -= video_row_size;
        return;
    }
    scrdown();
}

/* carriage return */
static void cr(void) {
    pos -= x << 1;
    x = 0;
}

/* delete */
static void del(void) {
    if (x != 0) {
        pos -= 2;
        x--;
        *(unsigned short *)pos = video_erase;
    }
}

/* 
 * ANSI escape sequence 'ESC [pJ' 
 * p=0 -- delete from the cursor to the bottom
 * p=1 -- delete from the top to the cursor
 * p=2 -- delete the whole screen
 */
static void escape_J(int p) {
    long count __asm__("cx");
    long start __asm__("di");

    switch (p) {
        case 0:
            count = (scr_end - pos) >> 1;
            start = pos;
            break;
        case 1:
            count = (pos - scr_start) >> 1;
            start = scr_start;
            break;
        case 2:
            count = video_cols * video_lines;
            start = scr_start;
            break;
        default:
            return;
    }
    __asm__(
        "cld; rep; stosw"
        :: "c" (count), "D" (start), "a" (video_erase)
        : "cx", "di"
    );
}

/* 
 * ANSI escape sequence 'ESC [pK' 
 * p=0 -- delete from the cursor to the end of line
 * p=1 -- delete from the start of line to the cursor
 * p=2 -- delete the whole line
 */
static void escape_K(int p) {
    long count __asm__("cx");
    long start __asm__("di");

    switch (p) {
        case 0:
            if (x >= video_cols) return;
            count = video_cols - x;
            start = pos;
            break;
        case 1:
            count = (x < video_cols)? x: video_cols;
            start = pos - (x << 1);
            break;
        case 2:
            count = video_cols;
            start = pos - (x << 1);
            break;
        default:
            return;
    }
    __asm__(
        "cld; rep; stosw"
        :: "c" (count), "D" (start), "a" (video_erase)
        : "cx", "di"
    );
}

/* 
 * ANSI escape sequence 'ESC [nm' 
 * n=0 -- normal
 * n=1 -- bold
 * n=4 -- underline
 * n=7 -- reverse display
 * n=27 -- normal
 */
void escape_m(void) {
    int i;
    for (i = 0; i <= npar; ++i) {
        switch (par[i]) {
            case 0:     attr = 0x07; break;
            case 1:     attr = 0x0f; break;
            case 4:     attr = 0x0f; break;
            case 7:     attr = 0x70; break;
            case 27:    attr = 0x07; break;
            default:
        }
    }
}

static inline void set_cursor(void) {
    cli();
    outb_p(14, video_reg_port);
    outb_p(((pos - video_mem_start) >> 9) & 0xff, video_val_port);
    outb_p(15, video_reg_port);
    outb_p(((pos - video_mem_start) >> 1) & 0xff, video_val_port);
    sti();
}

static void respond(struct tty_struct *tty) {
    char *p = RESPONSE;
    cli();
    while (*p) {
        PUTCH(*p, tty->read_q);
        p++;
    }
    sti();
    copy_to_cooked(tty);
}

static void insert_char(void) {
    int i = x;
    unsigned short tmp, origin = video_erase;
    unsigned short *p = (unsigned short *)pos;

    while (i++ < video_cols) {
        tmp = *p;
        *p = origin;
        origin = tmp;
        p++;
    }
}

static void insert_line(void) {
    int origin_top = top, origin_bottom = bottom;
    top = y;
    bottom = video_lines;
    scrdown();
    top = origin_top;
    bottom = origin_bottom;
}

static void delete_char(void) {
    int i = x;
    unsigned short *p = (unsigned short *)pos;

    if (x >= video_cols) return;
    while (++i < video_cols) {
        *p = *(p + 1);
        p++;
    }
    *p = video_erase;
}

static void delete_line(void) {
    int origin_top = top, origin_bottom = bottom;
    top = y;
    bottom = video_lines;
    scrup();
    top = origin_top;
    bottom = origin_bottom;
}

/* 
 * ANSI escape sequence 'ESC [n@' 
 * insert n characters at the cursor
 */
static void escape_at(unsigned int n) {
    if (n > video_cols)
        n = video_cols;
    else if (!n)
        n = 1;
    
    while (n--) insert_char();
}

/* 
 * ANSI escape sequence 'ESC [nL' 
 * insert n lines at the cursor
 */
static void escape_L(unsigned int n) {
    if (n >= video_lines)
        n = video_lines;
    else if (!n)
        n = 1;
    
    while (n--) insert_line();
}

/* 
 * ANSI escape sequence 'ESC [nP' 
 * delete n characters at the cursor
 */
static void escape_P(unsigned int n) {
    if (n > video_cols)
        n = video_cols;
    else if (!n)
        n = 1;
    
    while (n--) delete_char();
}

/* 
 * ANSI escape sequence 'ESC [nM' 
 * delete n lines at the cursor
 */
static void escape_M(unsigned int n) {
    if (n >= video_lines)
        n = video_lines;
    else if (!n)
        n = 1;
    
    while (n--) delete_line();
}

static int saved_x = 0, saved_y = 0;

static void save_cursor(void) {
    saved_x = x;
    saved_y = y;
}

static void restore_cursor(void) {
    gotoxy(saved_x, saved_y);
}

void con_write(struct tty_struct *tty) {
    int n = CHARS(tty->write_q);
    char ch;

    while (n--) {
        GETCH(tty->write_q, ch);
        switch (state) {
            case 0:     /* initial */
                if (ch > 31 && ch < 127) {    /* normal */
                    if (x >= video_cols) {
                        x -= video_cols;
                        pos -= video_row_size;
                        lf();
                    }
                    __asm__(
                        "movb _attr, %%ah\n\t"
                        "movw %%ax, %1\n\t"
                        :: "a" (ch), "m" (*(short *)pos)
                        : "ax"
                    );
                    pos += 2;
                    x++;
                } else if (ch == 033)       /* ESC */
                    state = 1;
                else if (ch == 10 || ch == 11 || ch == 12)  /* LF/VT/FF*/
                    lf();
                else if (ch == 13)          /* CR */
                    cr();
                else if (ch == ERASE_CHAR(tty)) /* DEL */
                    del();
                else if (ch == 8) {         /* backspace */
                    if (x) {
                        x--;
                        pos -= 2;
                    }
                } else if (ch == 9) {       /* TAB */
                    ch = 8 - (x % 7);
                    x += ch;
                    pos += ch << 1;
                    if (x > video_cols) {
                        x -= video_cols;
                        pos -= video_row_size;
                        lf();
                    }
                    ch = 9;
                } else if (ch == 7)         /* BEL */
                    beep();
                break;
            case 1:         /* ESC */
                state = 0;
                if (ch == '[')          state = 2;
                else if (ch == 'E')     gotoxy(0, y + 1);
                else if (ch == 'M')     ri();
                else if (ch == 'D')     lf();
                else if (ch == 'Z')     respond(tty);
                else if (ch == '7')     save_cursor();
                else if (ch == '8')     restore_cursor();
                break;
            case 2:         /* ESC [ */
                for (npar = 0; npar < NPAR; ++npar) par[npar] = 0;
                state = 3;
                if (ques = (ch == '?')) break;
            case 3:
                if (ch == ';' && npar < NPAR - 1) {
                    npar++;
                    break;
                } else if (ch >= '0' && ch <= '9') {
                    par[npar] = par[npar] * 10 + ch - '0';
                    break;
                } else 
                    state = 4;
            case 4:         /* complete ANSI escape sequence */
                state = 0;
                switch (ch) {
                    case 'G':
                    case '`':
                        if (par[0]) par[0]--;
                        gotoxy(par[0], y);
                        break;
                    case 'A':
                        if (!par[0]) par[0]++;
                        gotoxy(x, y - par[0]);
                        break;
                    case 'B':
                    case 'e':
                        if (!par[0]) par[0]++;
                        gotoxy(x, y + par[0]);
                        break;
                    case 'C':
                    case 'a':
                        if (!par[0]) par[0]++;
                        gotoxy(x + par[0], y);
                        break;
                    case 'D':
                        if (!par[0]) par[0]++;
                        gotoxy(x - par[0], y);
                        break;
                    case 'E':
                        if (!par[0]) par[0]++;
                        gotoxy(0, y + par[0]);
                        break;
                    case 'F':
                        if (!par[0]) par[0]++;
                        gotoxy(0, y - par[0]);
                        break;
                    case 'd':
                        if (!par[0]) par[0]++;
                        gotoxy(x, par[0]);
                        break;
                    case 'H':
                    case 'f':
                        if (!par[0]) par[0]++;
                        if (!par[1]) par[1]++;
                        gotoxy(par[1], par[0]);
                        break;
                    case 'J':
                        escape_J(par[0]);
                        break;
                    case 'K':
                        escape_K(par[0]);
                        break;
                    case 'L':
                        escape_L(par[0]);
                        break;
                    case 'M':
                        escape_M(par[0]);
                        break;
                    case 'P':
                        escape_P(par[0]);
                        break;
                    case '@':
                        escape_at(par[0]);
                        break;
                    case 'm':
                        escape_m();
                        break;
                    case 'r':
                        if (par[0]) par[0]--;
                        if (!par[1]) par[1] = video_lines;
                        if (par[0] < par[1] && par[1] <= video_lines) {
                            top = par[0];
                            bottom = par[1];
                        }
                        break;
                    case 's':
                        save_cursor();
                        break;
                    case 'u':
                        restore_cursor();
                        break;
                }
        }
    }
    set_cursor();
}

void con_init(void) {
    register unsigned char a;
    char *display_desc = "????";
    char *display_ptr;

    video_cols = ORIG_VIDEO_COLS;
    video_row_size = video_cols * 2;
    video_lines = ORIG_VIDEO_LINES;
    video_page = ORIG_VIDEO_PAGE;
    video_erase = 0x720;

    if (ORIG_VIDEO_MODE == 7) {      /* monochrome display */
        video_mem_start = 0xb0000;
        video_reg_port = 0x3b4;
        video_val_port = 0x3b5;
        if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) {
            video_type = VIDEO_EGAM;
            video_mem_end = 0xb8000;
            display_desc = "EGAM";
        } else {
            video_type = VIDEO_MDA;
            video_mem_end = 0xb2000;
            display_desc = "_MDA";
        }
    } else {        /* color display */
        video_mem_start = 0xb8000;
        video_reg_port = 0x3d4;
        video_val_port = 0x3d5;
        if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) {
            video_type = VIDEO_EGAC;
            video_mem_end = 0xbc000;
            display_desc = "EGAC";
        } else {
            video_type = VIDEO_CGA;
            video_mem_end = 0xba000;
            display_desc = "_CGA";
        }
    }

    display_ptr = ((char *)video_mem_start) + video_row_size - 8;
    while (*display_desc) {
        *display_ptr++ = *display_desc++;
        display_ptr++;
    }

    /* initialize for scrolling */
    scr_start = video_mem_start;
    scr_end = video_mem_start + video_row_size * video_lines;
    top = 0;
    bottom = video_lines;

    gotoxy(ORIG_X, ORIG_Y);
    set_trap_gate(0x21, &keyboard_int);
    outb_p(inb_p(0x21) & 0xfd, 0x21);
    a = inb_p(0x61);
    outb_p(a | 0x80, 0x61);
    outb(a, 0x61);
}

void beepstop(void) {
    outb(inb_p(0x61) & 0xfc, 0x61);
}

int beepcount = 0;

static void beep(void) {
    outb_p(inb_p(0x61) | 3, 0x61);
    outb_p(0xb6, 0x43);
    outb_p(0x37, 0x42);
    outb(0x06, 0x42);
    beepcount = HZ / 8;
}