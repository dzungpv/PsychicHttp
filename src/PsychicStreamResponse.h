#ifndef PsychicStreamResponse_h
#define PsychicStreamResponse_h

#include "ChunkPrinter.h"
#include "PsychicCore.h"
#include "PsychicResponse.h"
#include <string>

namespace PsychicHttp {

class PsychicRequest;

class PsychicStreamResponse : public PsychicResponseDelegate, public Print
{
  private:
    ChunkPrinter* _printer;
    uint8_t* _buffer;

  public:
    PsychicStreamResponse(PsychicResponse* response, const std::string& contentType);
    PsychicStreamResponse(PsychicResponse* response, const std::string& contentType, const std::string& name); // Download

    ~PsychicStreamResponse();

    esp_err_t beginSend();
    esp_err_t endSend();

    void flush() override;

    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t size) override;

#ifdef ARDUINO
    size_t copyFrom(Stream &stream);
#else
    size_t copyFrom(FILE *stream);
#endif

    using Print::write;
};

} // namespace PsychicHttp

#endif // PsychicStreamResponse_h
