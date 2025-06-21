#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "MD5Builder.h"

static uint8_t hex_char_to_byte(uint8_t c)
{
    return (c >= 'a' && c <= 'f') ? (c - ((uint8_t)'a' - 0xa)) :
           (c >= 'A' && c <= 'F') ? (c - ((uint8_t)'A' - 0xA)) :
           (c >= '0' && c <= '9') ? (c - (uint8_t)'0') : 0;
}

void MD5Builder::begin()
{
    memset(&_ctx, 0, sizeof(md5_context_s));
    MD5Init(&_ctx);
}

void MD5Builder::add(uint8_t* data, int len)
{
    MD5Update(&_ctx, data, len);
}

void MD5Builder::addHexString(const char *data)
{
    uint16_t i, len = strlen(data);
    uint8_t *tmp = (uint8_t*)malloc(len/2);
    if(tmp == NULL) {
        return;
    }
    for(i=0; i<len; i+=2) {
        uint8_t high = hex_char_to_byte(data[i]);
        uint8_t low = hex_char_to_byte(data[i+1]);
        tmp[i/2] = (high & 0x0F) << 4 | (low & 0x0F);
    }
    add(tmp, len/2);
    free(tmp);
}

void MD5Builder::calculate()
{
    MD5Final(_buf, &_ctx);
}

void MD5Builder::getBytes(uint8_t* output)
{
    memcpy(output, _buf, 16);
}

void MD5Builder::getChars(char* output)
{
    for (uint8_t i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", _buf[i]);
    }
}

std::string MD5Builder::toString()
{
    char out[33];
    getChars(out);
    out[32] = '\0';
    return std::string(out);
} 