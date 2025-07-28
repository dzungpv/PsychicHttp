#ifndef PsychicCore_h
#define PsychicCore_h

#define PH_TAG "psychic"

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

#include "esp_random.h"
#ifdef ARDUINO
#include <libb64/cencode.h>
#include "MD5Builder.h"
#else // ESP IDF using mbedtls for base64 and md5
#include <mbedtls/base64.h>
#include <mbedtls/md5.h>
#endif
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <list>
#include <map>
#include <string>
#include <functional>

#ifdef PSY_DEVMODE
  #include "ArduinoTrace.h"
#endif

namespace PsychicHttp {

enum HTTPAuthMethod {
  BASIC_AUTH,
  DIGEST_AUTH
};

std::string urlDecode(const char* encoded);

class PsychicHttpServer;
class PsychicRequest;
class PsychicResponse;
class PsychicWebSocketRequest;
class PsychicClient;

// filter function definition
typedef std::function<bool(PsychicRequest* request)> PsychicRequestFilterFunction;

// middleware function definition
typedef std::function<esp_err_t()> PsychicMiddlewareNext;
typedef std::function<esp_err_t(PsychicRequest* request, PsychicResponse* response, PsychicMiddlewareNext next)> PsychicMiddlewareCallback;

// client connect callback
typedef std::function<void(PsychicClient* client)> PsychicClientCallback;

// callback definitions
typedef std::function<esp_err_t(PsychicRequest* request, PsychicResponse* response)> PsychicHttpRequestCallback;
typedef std::function<esp_err_t(PsychicRequest* request, PsychicResponse* response, JsonVariant& json)> PsychicJsonRequestCallback;
typedef std::function<esp_err_t(PsychicRequest* request, const std::string& filename, uint64_t index, uint8_t* data, size_t len, bool final)> PsychicUploadCallback;

struct HTTPHeader {
    std::string field;
    std::string value;
};

class DefaultHeaders
{
    std::list<HTTPHeader> _headers;

  public:
    DefaultHeaders() {}

    void addHeader(const std::string& field, const std::string& value)
    {
      _headers.push_back({field, value});
    }

    void addHeader(const char* field, const char* value)
    {
      _headers.push_back({field, value});
    }

    const std::list<HTTPHeader>& getHeaders() { return _headers; }

    // delete the copy constructor, singleton class
    DefaultHeaders(DefaultHeaders const&) = delete;
    DefaultHeaders& operator=(DefaultHeaders const&) = delete;

    // single static class interface
    static DefaultHeaders& Instance()
    {
      static DefaultHeaders instance;
      return instance;
    }
};

} // namespace PsychicHttp

#endif // PsychicCore_h