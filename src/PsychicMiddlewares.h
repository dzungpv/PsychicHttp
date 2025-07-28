#ifndef PsychicMiddlewares_h
#define PsychicMiddlewares_h

#include "PsychicMiddleware.h"

#ifdef ARDUINO
  #include <Stream.h>
#else
  #include "Print.h" // Using local version support IDF and C++11
#endif
#include <http_status.h>
#include <string>

namespace PsychicHttp {

// curl-like logging middleware
class LoggingMiddleware : public PsychicMiddleware
{
  public:
    void setOutput(Print& output);

    esp_err_t run(PsychicRequest* request, PsychicResponse* response, PsychicMiddlewareNext next) override;

  private:
    Print* _out;
};

class AuthenticationMiddleware : public PsychicMiddleware
{
  public:
    AuthenticationMiddleware& setUsername(const char* username);
    AuthenticationMiddleware& setPassword(const char* password);

    AuthenticationMiddleware& setRealm(const char* realm);
    AuthenticationMiddleware& setAuthMethod(HTTPAuthMethod method);
    AuthenticationMiddleware& setAuthFailureMessage(const char* message);

    const std::string& getUsername() const { return _username; }
    const std::string& getPassword() const { return _password; }

    const std::string& getRealm() const { return _realm; }
    HTTPAuthMethod getAuthMethod() const { return _method; }
    const std::string& getAuthFailureMessage() const { return _authFailMsg; }

    bool isAllowed(PsychicRequest* request) const;

    esp_err_t run(PsychicRequest* request, PsychicResponse* response, PsychicMiddlewareNext next) override;

  private:
    std::string _username;
    std::string _password;

    std::string _realm;
    HTTPAuthMethod _method = BASIC_AUTH;
    std::string _authFailMsg;
};

class CorsMiddleware : public PsychicMiddleware
{
  public:
    CorsMiddleware& setOrigin(const char* origin);
    CorsMiddleware& setMethods(const char* methods);
    CorsMiddleware& setHeaders(const char* headers);
    CorsMiddleware& setAllowCredentials(bool credentials);
    CorsMiddleware& setMaxAge(uint32_t seconds);

    const std::string& getOrigin() const { return _origin; }
    const std::string& getMethods() const { return _methods; }
    const std::string& getHeaders() const { return _headers; }
    bool getAllowCredentials() const { return _credentials; }
    uint32_t getMaxAge() const { return _maxAge; }

    void addCORSHeaders(PsychicResponse* response);

    esp_err_t run(PsychicRequest* request, PsychicResponse* response, PsychicMiddlewareNext next) override;

  private:
    std::string _origin = "*";
    std::string _methods = "*";
    std::string _headers = "*";
    bool _credentials = true;
    uint32_t _maxAge = 86400;
};

} // namespace PsychicHttp

#endif
