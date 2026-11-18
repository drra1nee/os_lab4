#include "vowels.h"

#include <string.h>

static int is_latin_vowel(unsigned char c) {
    return c != '\0' && strchr("aeiouyAEIOUY", c) != NULL;
}

static int utf8_len(unsigned char c) {
    if (c < 0x80)           return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

// русские гласные в UTF-8: 2 байта, первый 0xD0 или 0xD1
static int is_cyr_vowel(unsigned char b0, unsigned char b1) {
    if (b0 == 0xD0) {
        switch (b1) {
        case 0x90: case 0x95: case 0x81: case 0x98: case 0x9E:  // А Е Ё И О
        case 0xA3: case 0xAB: case 0xAD: case 0xAE: case 0xAF:  // У Ы Э Ю Я
        case 0xB0: case 0xB5: case 0xB8: case 0xBE:             // а е и о
            return 1;
        }
    } else if (b0 == 0xD1) {
        switch (b1) {
        case 0x91: case 0x83: case 0x8B: case 0x8D: case 0x8E: case 0x8F:  // ё у ы э ю я
            return 1;
        }
    }
    return 0;
}

int remove_vowels(const char *src, char *dst) {
    const unsigned char *p = (const unsigned char *)src;
    int n = 0;

    while (*p) {
        int len = utf8_len(*p);

        for (int i = 1; i < len; i++)
            if (p[i] == '\0') { len = i; break; }

        int vowel = (len == 1) ? is_latin_vowel(p[0])
                  : (len == 2) ? is_cyr_vowel(p[0], p[1]) : 0;

        if (!vowel) {
            memcpy(dst + n, p, (size_t)len);
            n += len;
        }
        p += len;
    }
    dst[n] = '\0';
    return n;
}
