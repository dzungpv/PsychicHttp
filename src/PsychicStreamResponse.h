#ifndef PsychicStreamResponse_h
#define PsychicStreamResponse_h

#include "PsychicCore.h"
#include "PsychicResponse.h"
#include "ChunkPrinter.h"
#include <string>

class PsychicRequest;

class PsychicStreamResponse : public PsychicResponse, public Print
{
  private:
    ChunkPrinter *_printer;
    uint8_t *_buffer;
  public:
  
    PsychicStreamResponse(PsychicRequest *request, const std::string& contentType);
    PsychicStreamResponse(PsychicRequest *request, const std::string& contentType, const std::string& name); //Download
  
    ~PsychicStreamResponse();
  
    esp_err_t beginSend();
    esp_err_t endSend();

    void flush() override;

    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    // You can replace this with any custom buffer-read logic if needed
#if defined(ARDUINO)
    size_t copyFrom(Stream &stream);
#else
    size_t copyFrom(FILE *stream);
#endif

    using Print::write;
};

#endif // PsychicStreamResponse_h
