#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (src > dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if (src < dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    
    // Copy characters from src to dest, stopping at n or null terminator
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    
    // Pad remaining bytes with zeros if needed
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    
    return dest;
}

// String function implementations
char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    // Find the end of dest
    while (*d) d++;
    // Copy src to the end of dest
    while ((*d++ = *src++) != '\0');
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    
    do {
        if (*s1 != *s2++) return *(unsigned char *)s1 - *(unsigned char *)--s2;
        if (*s1++ == 0) break;
    } while (--n != 0);
    
    return 0;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    
    // Cast to unsigned char to correctly compare sign-extended 'c'
    c = (unsigned char)c;
    
    // Check each character and remember last occurrence
    while (*s) {
        if (*s == c) {
            last = s;
        }
        s++;
    }
    
    // Check for c being the null terminator
    if (c == 0) return (char *)s;
    
    return (char *)last;
}

size_t strlen(const char *s) {
    const char *sc = s;
    while (*sc) sc++;
    return sc - s;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == c) {
            return (char *)s;
        }
        s++;
    }
    
    if (c == '\0') {
        return (char *)s;  // Return pointer to the null terminator
    }
    
    return NULL;  // Character not found
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *token_start, *token_end;
    
    // If str is NULL, continue from saveptr
    if (str == NULL) {
        str = *saveptr;
    }
    
    // Skip leading delimiters
    while (*str && strchr(delim, *str)) {
        str++;
    }
    
    // If we reached the end, no more tokens
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }
    
    // This is the start of the token
    token_start = str;
    
    // Find the end of the token
    while (*str && !strchr(delim, *str)) {
        str++;
    }
    
    if (*str == '\0') {
        // End of string - no more tokens after this
        *saveptr = str;
    } else {
        // Terminate the token and move saveptr past it
        *str = '\0';
        *saveptr = str + 1;
    }
    
    return token_start;
}

// Helper functions needed by strtok_r
size_t strspn(const char *s, const char *accept) {
    const char *p = s;
    const char *a;
    
    while (*p) {
        for (a = accept; *a; a++) {
            if (*p == *a) break;
        }
        if (*a == '\0') return p - s;
        p++;
    }
    
    return p - s;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*s == *a++) return (char *)s;
        }
        s++;
    }
    
    return NULL;
}