#ifndef PsychicFileResponse_h
#define PsychicFileResponse_h

#include "PsychicCore.h"
#include "PsychicResponse.h"

class PsychicRequest;

class PsychicFileResponse: public PsychicResponse
{
  // using File = fs::File;
  // using FS = fs::FS;
  private:
    // File _content;
    FILE* _content;
    void _setContentType(const std::string& path);
  public:
    PsychicFileResponse(PsychicRequest *request, const std::string& path, const std::string& contentType=std::string(), bool download=false);
    PsychicFileResponse(PsychicRequest *request, FILE* content, const std::string& path, const std::string& contentType=std::string(), bool download=false);
    ~PsychicFileResponse();
    esp_err_t send();
};

#endif // PsychicFileResponse_h