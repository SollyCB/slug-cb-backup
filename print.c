// GCC_IGNORE_WARNINGS_BEGIN

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"

#define STB_SPRINTF_IMPLEMENTATION
#include "external/stb_sprintf.h"

#pragma GCC diagnostic pop
// GCC_IGNORE_WARNINGS_END

#include "print.h"
#include "defs.h"

typedef enum {
    PRINT_HEX_BIT    = 0x00000001,
    PRINT_BIN_BIT    = 0x00000002,
    PRINT_LZ_BIT     = 0x00000004,
    PRINT_UINT_BIT   = 0x00000008,
    PRINT_SINT_BIT   = 0x00000010,
    PRINT_FLOAT_BIT  = 0x00000020,
    PRINT_STRING_BIT = 0x00000040,
    PRINT_CHAR_BIT   = 0x00000080,
} Print_Flag_Bits;

typedef uint32 Print_Flags;

typedef enum {
    PRINT_INT_SIZE_NIL = 0,
    PRINT_INT_SIZE_8   = 8,
    PRINT_INT_SIZE_16  = 16,
    PRINT_INT_SIZE_32  = 32,
    PRINT_INT_SIZE_64  = 64,
} Print_Int_Size;

typedef struct {
    Print_Flags flags;
    Print_Int_Size int_size;
} Print_Config;

inline static void print_parse_int_dec(uint64 i, int *len, char *buf) {
    if (i == 0) {
        buf[*len] = '0';
        *len += 1;
        return ;
    }

    while(i > 0) {
        buf[*len] = (i % 10) + '0';
        *len += 1;
        i /= 10;
    }
}

inline static void print_parse_int_hex(uint64 i, int *len, char *buf) {
    if (i == 0) {
        buf[*len] = '0';
        *len += 1;
        return ;
    }

    char hex[] = {'a','b','c','d','e','f'};
    uint64 mask = 0x000000000000000f;
    int tmp;
    while(i > 0) {
        tmp = i & mask;

        if (tmp < 10)
            buf[*len] = tmp + '0';
        else
            buf[*len] = hex[tmp % 10];

        *len += 1;
        i >>= 4;
    }
}

inline static void print_parse_int_bin(uint64 i, int *len, char *buf) {
    if (i == 0) {
        buf[*len] = '0';
        *len += 1;
        return ;
    }

    uint64 mask = 0x0000000000000001;
    while(i > 0) {
        buf[*len] = (i & mask) + '0';
        *len += 1;
        i >>= 1;
    }
}

static void print_parse_int(Print_Config *config, uint64 i, int *buf_pos, char *print_buffer) {
    int max_zeros;

    switch(config->int_size) { // hex zeros
    case PRINT_INT_SIZE_8:
        max_zeros = 2;
        break;
    case PRINT_INT_SIZE_16:
        max_zeros = 4;
        break;
    case PRINT_INT_SIZE_32:
        max_zeros = 8;
        break;
    case PRINT_INT_SIZE_64:
        max_zeros = 16;
        break;
    default:
        max_zeros = 16;
        break;
    }

    char int_buf[96];
    int int_pos = 0;
    if (config->flags & PRINT_HEX_BIT) {
        print_parse_int_hex(i, &int_pos, int_buf);

        print_buffer[*buf_pos + 0] = '0';
        print_buffer[*buf_pos + 1] = 'x';

        *buf_pos += 2;
    } else if (config->flags & PRINT_BIN_BIT) {
        print_parse_int_bin(i, &int_pos, int_buf);

        print_buffer[*buf_pos + 0] = '0';
        print_buffer[*buf_pos + 1] = 'b';

        max_zeros *= 4;
        *buf_pos += 2;
    } else {
        print_parse_int_dec(i, &int_pos, int_buf);
        max_zeros = 0;
    }

    if (config->flags & PRINT_LZ_BIT) {
        int zeros;
        if (i > 0) {
            zeros = clz64(i);
            zeros &= max_zeros - 1;
        } else {
            zeros = max_zeros;
        }
        for(int j = 0; j < zeros; ++j) {
            int_buf[int_pos] = '0';
            int_pos++;
        }
    }

    // reverse int
    for(int j = 0; j < int_pos; ++j) {
        print_buffer[*buf_pos] = int_buf[(int_pos - 1) - j];
        *buf_pos += 1;
    }
}
inline static void print_parse_signed_int(Print_Config *config, int64 i, int *buf_pos, char *print_buffer) {
    if (i < 0) {
        print_buffer[*buf_pos] = '-';
        *buf_pos += 1;
        i = -i;
    }
    print_parse_int(config, (uint64)i, buf_pos, print_buffer);
}
inline static void print_parse_unsigned_int(Print_Config *config, uint64 i, int *buf_pos, char *print_buffer) {
    print_parse_int(config, i, buf_pos, print_buffer);
}

typedef enum {
    PRINT_VALUE_STRING,
    PRINT_VALUE_CHAR,
    PRINT_VALUE_UINT,
    PRINT_VALUE_SINT,
    PRINT_VALUE_FLOAT,
    PRINT_VALUE_HEX,
    PRINT_VALUE_BIN,
    PRINT_VALUE_LZ,
} Print_Value;

inline static bool print_check_config_flags(Print_Flags flags, Print_Value value) {
    switch(value) {
    case PRINT_VALUE_STRING:
    case PRINT_VALUE_CHAR:
    case PRINT_VALUE_FLOAT:
        return flags == 0;
    case PRINT_VALUE_SINT:
    case PRINT_VALUE_UINT:
        flags &= ~(PRINT_HEX_BIT | PRINT_BIN_BIT | PRINT_LZ_BIT);
        return flags == 0;
    case PRINT_VALUE_HEX:
        flags &= PRINT_STRING_BIT | PRINT_FLOAT_BIT | PRINT_CHAR_BIT | PRINT_HEX_BIT;
        return flags == 0;
    case PRINT_VALUE_BIN:
        flags &= PRINT_STRING_BIT | PRINT_FLOAT_BIT | PRINT_CHAR_BIT | PRINT_BIN_BIT;
        return flags == 0;
    case PRINT_VALUE_LZ:
        flags &= PRINT_STRING_BIT | PRINT_FLOAT_BIT | PRINT_CHAR_BIT | PRINT_LZ_BIT;
        return flags == 0;
    default:
        assert(false && "Invalid Flag Check");
        return false;
    }
}

void string_format_backend(char *format_buffer, const char *fmt, va_list args) { // args must have been started
    int buf_pos = 0;
    char c;
    char *s;
    uint64 u;
    int64 i;
    double f;

    Print_Config config = {};
    bool is_ident    = false;
    bool parse_sint  = false;
    bool parse_uint  = false;
    // bool parse_float = false; -Wunused

    int tmp;
    // char last_char = 0; -Wunused
    for(int j = 0; fmt[j] != 0; ++j) {

        if (fmt[j] != '%' && fmt[j] != '-') {
            format_buffer[buf_pos] = fmt[j];
            buf_pos++;
            continue;
        } else if (fmt[j] == '-') {
            if (fmt[j+1] == '%') {
                format_buffer[buf_pos + 0] = fmt[j + 0];
                format_buffer[buf_pos + 1] = fmt[j + 1];
                buf_pos += 2;
                j++;
            } else {
                format_buffer[buf_pos] = fmt[j];
                buf_pos++;
            }
            continue;
        } else {
            is_ident = true;

            config   = (Print_Config){};
            parse_sint  = false;
            parse_uint  = false;
            // parse_float = false; -Wunused

            j++;
            while(is_ident && fmt[j] != 0) {
                switch(fmt[j]) {
                case 0:
                    goto not_ident;
                case 'f':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_FLOAT)) {
                        goto not_ident;
                    }
                    f = va_arg(args, double);
                    buf_pos += stbsp_sprintf(format_buffer + buf_pos, "%f", f);
                    j++;
                    goto not_ident;
                case 'c':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_CHAR)) {
                        goto not_ident;
                    }
                    c = (char)va_arg(args, int);
                    format_buffer[buf_pos] = c;
                    buf_pos++;

                    j++;
                    goto not_ident;
                case 's':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_STRING)) {
                        goto not_ident;
                    }
                    s = va_arg(args, char*);
                    tmp = strlen(s);
                    memcpy(format_buffer + buf_pos, s, tmp);
                    buf_pos += tmp;

                    j++;
                    goto not_ident;
                case 'h':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_HEX)) {
                        j++;
                        goto not_ident;
                    } else {
                        config.flags |= PRINT_HEX_BIT;
                    }
                    break;
                case 'b':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_BIN)) {
                        j++;
                        goto not_ident;
                    } else {
                        config.flags |= PRINT_BIN_BIT;
                    }
                    break;
                case 'z':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_LZ)) {
                        j++;
                        goto not_ident;
                    } else {
                        config.flags |= PRINT_LZ_BIT;
                    }
                    break;
                case 'i':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_SINT)) {
                        j++;
                        goto not_ident;
                    } else {
                        i = va_arg(args, int64);
                        parse_sint = true;
                    }
                    break;
                case 'u':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_UINT)) {
                        j++;
                        goto not_ident;
                    } else {
                        u = va_arg(args, uint64);
                        parse_uint = true;
                    }
                    break;
                default:
                    goto not_ident;
                }
                j++;
            }

            not_ident:
            j--; // ensure next loop iteration sees non ident value

            if (parse_uint) {
                print_parse_unsigned_int(&config, u, &buf_pos, format_buffer);
                parse_uint = false;
            } else if (parse_sint) {
                print_parse_signed_int(&config, i, &buf_pos, format_buffer);
                parse_sint = false;
            }
        }
    }

    va_end(args);
    format_buffer[buf_pos] = 0;
}

