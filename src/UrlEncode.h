// https://github.com/plageoj/urlencode port to using with native cpp
#ifndef _PLAGEOJ_URLENCODE_H
#define _PLAGEOJ_URLENCODE_H

#include <string>

/**
 * Percent-encodes a string.
 * @param msg UTF-8 string to encode.
 * @returns Percent-encoded string.
 */
std::string urlEncode(const char *msg);

/**
 * Percent-encodes a string.
 * @param msg UTF-8 string to encode.
 * @returns Percent-encoded string.
 */
std::string urlEncode(const std::string& msg);

#endif