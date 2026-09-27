#include "kernel.h"

void* memset(void* dst, int v, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)v;
    return dst;
}

void* memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

int strlen(const char* s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(*a) - (int)(*b);
}

char* strcpy(char* d, const char* s) {
    char* r = d;
    while ((*d++ = *s++));
    return r;
}

char* strcat(char* d, const char* s) {
    char* p = d + strlen(d);
    while ((*p++ = *s++));
    return d;
}

void itoa(int n, char* buf) {
    int i = 0, neg = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    if (n < 0) { neg = 1; n = -n; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    if (neg) buf[i++] = '-';
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i - 1 - j]; buf[i - 1 - j] = t;
    }
    buf[i] = 0;
}
