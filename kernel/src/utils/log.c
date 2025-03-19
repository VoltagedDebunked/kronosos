#include <utils/log.h>
#include <drivers/serial/serial.h>
#include <stdarg.h>

#define LOG_SERIAL_PORT SERIAL_COM1
#define LOG_BUFFER_SIZE 256

static log_level_t current_log_level = LOG_LEVEL_INFO;
static bool log_initialized = false;
static char log_buffer[LOG_BUFFER_SIZE];

static const char *log_level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "CRITICAL"
};

bool log_init(log_level_t level) {
    current_log_level = level;
    
    bool result = serial_init(LOG_SERIAL_PORT, SERIAL_BAUD_115200);
    
    if (result) {
        log_initialized = true;
        log_message(LOG_LEVEL_INFO, "Logging system initialized");
    }
    
    return result;
}

static int vsprintf(char *buffer, const char *fmt, va_list args) {
    char *ptr = buffer;
    
    while (*fmt != '\0') {
        if (*fmt == '%') {
            fmt++;
            
            // Handle width specifier like %02x
            int width = 0;
            int zero_pad = 0;
            
            if (*fmt == '0') {
                zero_pad = 1;
                fmt++;
            }
            
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            switch (*fmt) {
                case 's': {
                    const char *str = va_arg(args, const char *);
                    if (!str) str = "(null)";
                    while (*str) {
                        *ptr++ = *str++;
                    }
                    break;
                }
                
                case 'c':
                    *ptr++ = (char)va_arg(args, int);
                    break;
                
                case 'd': {
                    int value = va_arg(args, int);
                    
                    if (value < 0) {
                        *ptr++ = '-';
                        value = -value;
                    }
                    
                    if (value == 0) {
                        *ptr++ = '0';
                    } else {
                        // Count digits
                        int digits = 0;
                        int temp = value;
                        while (temp > 0) {
                            digits++;
                            temp /= 10;
                        }
                        
                        // Pad with zeros if necessary
                        if (zero_pad && width > digits) {
                            for (int i = 0; i < width - digits; i++) {
                                *ptr++ = '0';
                            }
                        }
                        
                        // Convert to string
                        char *p = ptr + digits;
                        ptr = p;
                        while (value > 0) {
                            *--p = '0' + (value % 10);
                            value /= 10;
                        }
                    }
                    break;
                }
                
                case 'u': {
                    unsigned int value = va_arg(args, unsigned int);
                    
                    if (value == 0) {
                        *ptr++ = '0';
                    } else {
                        // Count digits
                        int digits = 0;
                        unsigned int temp = value;
                        while (temp > 0) {
                            digits++;
                            temp /= 10;
                        }
                        
                        // Pad with zeros if necessary
                        if (zero_pad && width > digits) {
                            for (int i = 0; i < width - digits; i++) {
                                *ptr++ = '0';
                            }
                        }
                        
                        // Convert to string
                        char *p = ptr + digits;
                        ptr = p;
                        while (value > 0) {
                            *--p = '0' + (value % 10);
                            value /= 10;
                        }
                    }
                    break;
                }
                
                case 'x':
                case 'X': {
                    unsigned int value = va_arg(args, unsigned int);
                    int uppercase = (*fmt == 'X');
                    
                    if (value == 0) {
                        *ptr++ = '0';
                    } else {
                        // Count digits
                        int digits = 0;
                        unsigned int temp = value;
                        while (temp > 0) {
                            digits++;
                            temp >>= 4;
                        }
                        
                        // Pad with zeros if necessary
                        if (zero_pad && width > digits) {
                            for (int i = 0; i < width - digits; i++) {
                                *ptr++ = '0';
                            }
                        }
                        
                        // Convert to string
                        char *p = ptr + digits;
                        ptr = p;
                        while (value > 0) {
                            int digit = value & 0xF;
                            if (digit < 10) {
                                *--p = '0' + digit;
                            } else {
                                *--p = (uppercase ? 'A' : 'a') + (digit - 10);
                            }
                            value >>= 4;
                        }
                    }
                    break;
                }
                
                case 'p': {
                    *ptr++ = '0';
                    *ptr++ = 'x';
                    
                    uintptr_t value = (uintptr_t)va_arg(args, void *);
                    
                    // Handle width for pointers (usually 16 digits for 64-bit)
                    int digits = 16;  // 64-bit address = 16 hex digits
                    
                    if (value == 0) {
                        // Special case for NULL
                        *ptr++ = '0';
                    } else {
                        // Convert to string
                        for (int i = digits - 1; i >= 0; i--) {
                            int digit = (value >> (i * 4)) & 0xF;
                            if (digit < 10) {
                                *ptr++ = '0' + digit;
                            } else {
                                *ptr++ = 'A' + (digit - 10);
                            }
                        }
                    }
                    break;
                }
                
                case '%':
                    *ptr++ = '%';
                    break;
                
                case 'l':  // Handle 'llX' for long long
                    if (*(fmt + 1) == 'l' && (*(fmt + 2) == 'X' || *(fmt + 2) == 'x')) {
                        fmt += 2;  // Skip 'll'
                        
                        unsigned long long value = va_arg(args, unsigned long long);
                        int uppercase = (*fmt == 'X');
                        
                        if (value == 0) {
                            *ptr++ = '0';
                        } else {
                            // Count digits
                            int digits = 0;
                            unsigned long long temp = value;
                            while (temp > 0) {
                                digits++;
                                temp >>= 4;
                            }
                            
                            // Pad with zeros if necessary
                            if (zero_pad && width > digits) {
                                for (int i = 0; i < width - digits; i++) {
                                    *ptr++ = '0';
                                }
                            }
                            
                            // Convert to string
                            char *p = ptr + digits;
                            ptr = p;
                            while (value > 0) {
                                int digit = value & 0xF;
                                if (digit < 10) {
                                    *--p = '0' + digit;
                                } else {
                                    *--p = (uppercase ? 'A' : 'a') + (digit - 10);
                                }
                                value >>= 4;
                            }
                        }
                    } else {
                        *ptr++ = '%';
                        *ptr++ = 'l';
                    }
                    break;
                
                default:
                    *ptr++ = '%';
                    *ptr++ = *fmt;
                    break;
            }
        } else {
            *ptr++ = *fmt;
        }
        
        fmt++;
    }
    
    *ptr = '\0';
    return ptr - buffer;
}


static void printf_serial(const char *fmt, ...) {
    if (!log_initialized) {
        return;
    }
    
    va_list args;
    va_start(args, fmt);
    
    vsprintf(log_buffer, fmt, args);
    
    va_end(args);
    
    serial_write_string(LOG_SERIAL_PORT, log_buffer);
}

void log_printf(log_level_t level, const char *fmt, ...) {
    if (!log_initialized || level < current_log_level) {
        return;
    }
    
    printf_serial("[%s] ", log_level_names[level]);
    
    va_list args;
    va_start(args, fmt);
    
    vsprintf(log_buffer, fmt, args);
    
    va_end(args);
    
    serial_write_string(LOG_SERIAL_PORT, log_buffer);
    serial_write_byte(LOG_SERIAL_PORT, '\r');
    serial_write_byte(LOG_SERIAL_PORT, '\n');
}

void log_message(log_level_t level, const char *msg) {
    if (!log_initialized || level < current_log_level) {
        return;
    }
    
    printf_serial("[%s] %s\r\n", log_level_names[level], msg);
}