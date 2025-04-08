#include "esp_log.h"

#ifndef PsychicCore_h
#define PsychicCore_h

#define PH_TAG "psychic"

// version numbers
#define PSYCHIC_HTTP_VERSION_MAJOR 1
#define PSYCHIC_HTTP_VERSION_MINOR 1
#define PSYCHIC_HTTP_VERSION_PATCH 0

#ifndef MAX_COOKIE_SIZE
#define MAX_COOKIE_SIZE 512
#endif

#ifndef FILE_CHUNK_SIZE
#define FILE_CHUNK_SIZE 8 * 1024
#endif

#ifndef STREAM_CHUNK_SIZE
#define STREAM_CHUNK_SIZE 1024
#endif

#ifndef MAX_UPLOAD_SIZE
#define MAX_UPLOAD_SIZE (2048 * 1024) // 2MB
#endif

#ifndef MAX_REQUEST_BODY_SIZE
#define MAX_REQUEST_BODY_SIZE (16 * 1024) // 16K
#endif

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <esp_http_server.h>
#include <map>
#include <list>
// #include <libb64/cencode.h>
#include "esp_random.h"
// #include "MD5Builder.h"
// #include <UrlEncode.h>
// #include "FS.h"
#include <ArduinoJson.h>
#include <functional> // Required for std::function
#include "esp_err.h"  // Required for esp_err_t

enum HTTPAuthMethod
{
  BASIC_AUTH,
  DIGEST_AUTH
};

void urlDecode(const char* encoded, char* decoded, size_t buffer_size);

class PsychicHttpServer;
class PsychicRequest;
// class PsychicWebSocketRequest;
class PsychicClient;

// Filter function definition
typedef bool (*PsychicRequestFilterFunction)(PsychicRequest *request);

// Client connect callback
typedef void (*PsychicClientCallback)(PsychicClient *client);

// // Callback definitions
typedef esp_err_t (*PsychicHttpRequestCallback)(PsychicRequest *request);
typedef esp_err_t (*PsychicJsonRequestCallback)(PsychicRequest *request, JsonVariant json);

typedef bool (*PsychicRequestFilterFunction)(PsychicRequest *request);
typedef void (*PsychicClientCallback)(PsychicClient *client);

struct HTTPHeader
{
  char *field;
  char *value;
};

class DefaultHeaders
{
  std::list<HTTPHeader> _headers;

public:
  DefaultHeaders() {}

  void addHeader(const char * field, const char * value)
  {
    HTTPHeader header;
    if (field && value)
    {
      // these are just going to stick around forever.
      header.field = (char *)malloc(strlen(field) + 1);
      header.value = (char *)malloc(strlen(value) + 1);

      strlcpy(header.field, field, strlen(field) + 1);
      strlcpy(header.value, value, strlen(value) + 1);

      _headers.push_back(header);
    }
    else
    {
      ESP_LOGW(PH_TAG, "CHECK THIS, NULLPTR");
    }
  }

  const std::list<HTTPHeader>& getHeaders() const { return _headers; }

  // delete the copy constructor, singleton class
  DefaultHeaders(DefaultHeaders const &) = delete;
  DefaultHeaders &operator=(DefaultHeaders const &) = delete;

  // single static class interface
  static DefaultHeaders &Instance()
  {
    static DefaultHeaders instance;
    return instance;
  }
};

#endif // PsychicCore_h