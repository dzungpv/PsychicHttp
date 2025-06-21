/*
    MD5Builder.h - Main header file for the MD5Builder library
    Copyright (c) 2015 Hristo Gochkov. All rights reserved.
    This file is part of the esp8266 core for Arduino environment.
*/
#ifndef MD5Builder_H
#define MD5Builder_H

#include <string>
#include "esp32/rom/md5_hash.h"

class MD5Builder
{
  public:
    void begin();
    void add(uint8_t* data, int len);
    void addHexString(const char *data);
    void calculate();
    void getBytes(uint8_t* output);
    void getChars(char* output);
    std::string toString();

  private:
    struct md5_context_s _ctx;
    uint8_t _buf[16];
};

#endif // MD5Builder_H 