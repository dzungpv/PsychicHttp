#ifndef PsychicEndpoint_h
#define PsychicEndpoint_h

#include "PsychicCore.h"

class PsychicHandler;

#ifdef ENABLE_ASYNC
  #include "async_worker.h"
#endif

class PsychicEndpoint
{
  friend PsychicHttpServer;

  private:
    PsychicHttpServer *_server;
    const char *_uri;
    http_method _method;
    PsychicHandler *_handler;

  public:
    PsychicEndpoint();
    PsychicEndpoint(PsychicHttpServer *server, http_method method, const char * uri);

    PsychicEndpoint *setHandler(PsychicHandler *handler);
    PsychicHandler *handler();

    PsychicEndpoint* setFilter(PsychicRequestFilterFunction fn);
    PsychicEndpoint* setAuthentication(const char *username, const char *password, HTTPAuthMethod method = BASIC_AUTH, const char *realm = "", const char *authFailMsg = "");

    const char *uri();

    static esp_err_t requestCallback(httpd_req_t *req);
};

#endif // PsychicEndpoint_h