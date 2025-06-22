#ifndef PsychicRequest_h
#define PsychicRequest_h

#include "PsychicCore.h"
#include "PsychicHttpServer.h"
#include "PsychicClient.h"
#include "PsychicWebParameter.h"
#include "PsychicResponse.h"
#include <string>
#include <mbedtls/base64.h>
#include <mbedtls/md5.h>

typedef std::map<std::string, std::string> SessionData;

enum Disposition { NONE, INLINE, ATTACHMENT, FORM_DATA};

struct ContentDisposition {
  Disposition disposition;
  std::string filename;
  std::string name;
};

class PsychicRequest {
  friend PsychicHttpServer;

  protected:
    PsychicHttpServer *_server;
    httpd_req_t *_req;
    SessionData *_session;
    PsychicClient *_client;

    http_method _method;
    const char *_uri;
    std::string _query;
    std::string _body;

    std::list<PsychicWebParameter*> _params;

    void _addParams(const std::string& params, bool post);
    void _parseGETParams();
    void _parsePOSTParams();

    std::string _extractParam(const std::string &authReq, const std::string &param, char delimit);
    const std::string _getRandomHexString();

  public:
    PsychicRequest(PsychicHttpServer *server, httpd_req_t *req);
    virtual ~PsychicRequest();

    void *_tempObject;

    PsychicHttpServer * server();
    httpd_req_t * request();
    virtual PsychicClient * client();

    bool isMultipart();
    esp_err_t loadBody();

    std::string header(const char *name);
    bool hasHeader(const char *name);

    static void freeSession(void *ctx);
    bool hasSessionKey(const char* key);
    const std::string getSessionKey(const std::string& key);
    void setSessionKey(const std::string& key, const std::string& value);

    bool hasCookie(const char * key);
    std::string getCookie(const char * key);

    http_method method() const;       // returns the HTTP method used as enum value (eg. HTTP_GET)
    std::string methodStr();   // returns the HTTP method used as a string (eg. "GET")
    std::string path();        // returns the request path (eg /page?foo=bar returns "/page")
    const char * uri() const;        // returns the full request uri (eg /page?foo=bar)
    const std::string& query() const;      // returns the request query data (eg /page?foo=bar returns "foo=bar")
    std::string  host();        // returns the requested host (request to http://psychic.local/foo will return "psychic.local")
    std::string  contentType(); // returns the Content-Type header value
    size_t contentLength() const;     // returns the Content-Length header value
    const std::string& body();       // returns the body of the request
    const ContentDisposition getContentDisposition();

    const std::string& queryString() { return query(); }  //compatability function.  same as query()
    const char * url() { return uri(); }            //compatability function.  same as uri()

    void loadParams();
    PsychicWebParameter * addParam(PsychicWebParameter* param);
    PsychicWebParameter * addParam(const std::string& name, const std::string& value, bool decode = true, bool post = false);
    bool hasParam(const char *key);
    PsychicWebParameter * getParam(const char *name);

    const char* getFilename();

    bool authenticate(const char * username, const char * password);
    esp_err_t requestAuthentication(HTTPAuthMethod mode, const char* realm, const char* authFailMsg);

    esp_err_t redirect(const char *url);
    esp_err_t reply(int code);
    esp_err_t reply(const char *content);
    esp_err_t reply(int code, const char *contentType, const char *content);

};

#endif // PsychicRequest_h