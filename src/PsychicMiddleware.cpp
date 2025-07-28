#include "PsychicMiddleware.h"

namespace PsychicHttp {

esp_err_t PsychicMiddlewareFunction::run(PsychicRequest* request, PsychicResponse* response, PsychicMiddlewareNext next)
{
  return _fn(request, request->response(), next);
}

} // namespace PsychicHttp
