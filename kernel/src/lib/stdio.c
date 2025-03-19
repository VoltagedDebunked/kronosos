#include <lib/stdio.h>
#include <stdarg.h>
#include <lib/string.h>

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    int result;
    
    va_start(args, format);
    result = vsnprintf(str, size, format, args);
    va_end(args);
    
    return result;
}

int vsnprintf(char *str, size_t size, const char *format, va_list args) {
    if (size == 0) return 0;
    
    char *str_start = str;
    size_t remaining = size - 1; // Leave space for null terminator
    
    while (*format && remaining > 0) {
        if (*format != '%') {
            *str++ = *format++;
            remaining--;
            continue;
        }
        
        // Process format specifier
        format++; // Skip '%'
        
        // Handle %% case
        if (*format == '%') {
            *str++ = '%';
            format++;
            remaining--;
            continue;
        }
        
        // Process format specifier
        switch (*format) {
            case 's': { // String
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                
                size_t len = strlen(s);
                if (len > remaining) len = remaining;
                
                for (size_t i = 0; i < len; i++) {
                    *str++ = *s++;
                    remaining--;
                }
                break;
            }
            
            case 'c': { // Character
                char c = (char)va_arg(args, int);
                *str++ = c;
                remaining--;
                break;
            }
            
            case 'd': { // Decimal integer
                int value = va_arg(args, int);
                
                // Handle negative numbers
                if (value < 0) {
                    *str++ = '-';
                    value = -value;
                    remaining--;
                    if (remaining == 0) break;
                }
                
                // Get digits
                char buffer[12]; // More than enough for 32-bit int
                char *p = buffer;
                
                if (value == 0) {
                    *p++ = '0';
                } else {
                    while (value > 0 && p < buffer + sizeof(buffer)) {
                        *p++ = '0' + (value % 10);
                        value /= 10;
                    }
                }
                
                // Copy digits in reverse order
                while (p > buffer && remaining > 0) {
                    *str++ = *(--p);
                    remaining--;
                }
                break;
            }
            
            case 'x': { // Hexadecimal
                unsigned int value = va_arg(args, unsigned int);
                
                // Get hex digits
                char buffer[8]; // Enough for 32-bit int
                char *p = buffer;
                
                if (value == 0) {
                    *p++ = '0';
                } else {
                    while (value > 0 && p < buffer + sizeof(buffer)) {
                        int digit = value % 16;
                        if (digit < 10)
                            *p++ = '0' + digit;
                        else
                            *p++ = 'a' + (digit - 10);
                        value /= 16;
                    }
                }
                
                // Copy digits in reverse order
                while (p > buffer && remaining > 0) {
                    *str++ = *(--p);
                    remaining--;
                }
                break;
            }
            
            default:
                // Unrecognized format specifier, output as-is
                *str++ = '%';
                *str++ = *format;
                remaining -= 2;
                break;
        }
        
        format++;
    }
    
    // Null-terminate the string
    *str = '\0';
    
    // Return the number of characters written (excluding null terminator)
    return str - str_start;
}