#ifndef PsychicFileResponse_h
#define PsychicFileResponse_h

#include "PsychicCore.h"
#include "PsychicResponse.h"
#include <string>

namespace PsychicHttp {

class PsychicRequest;

class PsychicFileResponse : public PsychicResponseDelegate
{
    // using File = fs::File;
    //using FS = fs::FS;

  protected:
    FILE* _content;
    void _setContentTypeFromPath(const std::string& path);
    bool endsWith(const std::string& value, const std::string& ending);

  public:
    PsychicFileResponse(PsychicResponse* response, const std::string& path, const std::string& contentType = std::string(), bool download = false);
    PsychicFileResponse(PsychicResponse* response, FILE* content, const std::string& path, const std::string& contentType = std::string(), bool download = false);
    ~PsychicFileResponse();
    esp_err_t send();
};

} // namespace PsychicHttp

#endif // PsychicFileResponse_h