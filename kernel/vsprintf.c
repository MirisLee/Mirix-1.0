/*
 * Mirix 1.0/kernel/vsprintf.c
 * (C) 2022 Miris Lee
 */

#include <stdarg.h>
#include <string.h>

#define is_digit(ch) ((ch) >= '0' && (ch) <= '9')

/* change string to int and move the pointer */
static int mov_atoi(const char **s) {
    int i = 0;
    while (is_digit(**s))
        i = i * 10 + *((*s)++) - '0';
    return i;
}

#define ZERO_PAD    1
#define SIGN        2
#define PLUS        4
#define SPACE_PLUS  8
#define LEFT        16
#define HEX_OCT     32
#define LOWER       64

#define do_div(n, base) ({ \
    int __res; \
    __asm__("divl %4": "=a" (n), "=d" (res): "" (n), "1" (0), "r" (base)); \
    __res; \
})

static char *num_to_str(char *str, int num, int base, int size, 
    int precision, int type) {
    
    int i;
    char ch, sign, tmp[36];
    const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    if (type & LOWER)
        digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (type & LEFT) type &= ~ZERO_PAD;
    if (base < 2 || base > 36) return 0;

    ch = (type & ZERO_PAD)? '0': ' ';
    if (type & SIGN && num < 0) {
        sign = '-';
        num = -num;
    } else {
        sign = (type & PLUS)? '+': ((type & SPACE_PLUS)? ' ': 0);
    }
    if (sign) size--;
    if (type & HEX_OCT)
        if (base == 16) size -= 2;
        else if (base == 8) size--;

    i = 0;
    if (num == 0)
        tmp[i++] = '0';
    else
        while(num != 0)
            tmp[i++] = digits[do_div(num, base)];
    if (i > precision) precision = i;
    size -= precision;

    if (!(type & (ZERO_PAD | LEFT)))
        while (size-- > 0) *str++ = ' ';
    if (sign) *str++ = sign;
    if (type & HEX_OCT)
        if (base == 8) {
            *str++ = '0';
        } else if (base == 16) {
            *str++ = '0';
            *str++ = digits[33];
        }
    if (!(type & LEFT))
        while (size-- > 0) *str++ = ch;
    while (i < precision--) *str++ = '0';
    while (i-- > 0) *str++ = tmp[i];
    while (size-- > 0) *str++ = ' ';
    return str;
}

int vsprintf(char *buf, const char *fmt, va_list args) {
    int len, i, *ip;
    char *str, *s;
    int flags, field_width, precision, qualifier;

    for (str = buf; *fmt; ++fmt) {
        if (*fmt != '%') {
            *str++ = *fmt;
            continue;
        }
        /* flags */
    loop:
        ++fmt;
        switch (*fmt) {
            case '-': flags |= LEFT; goto loop;
            case '+': flags |= PLUS; goto loop;
            case ' ': flags |= SPACE_PLUS; goto loop;
            case '#': flags |= HEX_OCT; goto loop;
            case '0': flags |= ZERO_PAD; goto loop;
        }
        /* field width */
        field_width = -1;
        if (is_digit(*fmt))
            field_width = mov_atoi(&fmt);
        else if (*fmt == '*') {
            ++fmt;
            field_width = va_arg(args, int);
            if (field_width < 0) {
                field_width = -field_width;
                flags |= LEFT;
            }
        }
        /* precision */
        precision = -1;
        if (*fmt == '.') {
            ++fmt;
            if (is_digit(*fmt))
                precision = mov_atoi(&fmt);
            else if (*fmt == '*')
                precision = va_arg(args, int);
            if (precision < 0) precision = 0;
        }
        /* qualifier */
        qualifier = -1;
        if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
            qualifer = *fmt;
            ++fmt;
        }

        switch (*fmt) {
            case 'c':
                if (!(flags & LEFT))
                    while (--field_width > 0) *str++ = ' ';
                *str++ = (unsigned char)va_arg(args, int);
                while (--field_width > 0) *str++ = ' ';
                break;
            case 's':
                s = va_arg(args, char *);
                len = strlen(s);
                if (precision < 0)
                    precision = len;
                else if (len > precision)
                    len = precision;
                if (!(flags & LEFT))
                    while (len < field_width--) *str++ = ' ';
                for (i = 0; i < len; ++i) *str++ = *s++;
                while (len < field_width--) *str++ = ' ';
                break;
            case 'o':
                str = num_to_str(str, va_arg(args, unsigned long), 8, 
                    field_width, precision, flags);
                break;
            case 'p':
                if (field_width == -1) {
                    field_width = 8;
                    flags |= ZERO_PAD;
                }
                str = num_to_str(str, (unsigned long)va_arg(args, void *), 16, 
                    field_width, precision, flags);
                break;
            case 'x':
                flags |= LOWER;
            case 'X':
                str = num_to_str(str, va_arg(args, unsigned long), 16, 
                    field_width, precision, flags);
                break;
            case 'd':
            case 'i':
                flags |= SIGN;
            case 'u':
                str = num_to_str(str, va_arg(args, unsigned long), 10, 
                    field_width, precision, flags);
                break;
            case 'n':
                ip = va_arg(args, int *);
                *ip = (str - buf);
                break;
            default:
                *str++ = '%';
                if (*fmt)
                    *str++ = *fmt;
                else 
                    --fmt;
                break;
        }
    }
    *str = '\0';
    return (str - buf);
}