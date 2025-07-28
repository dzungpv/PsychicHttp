
#ifndef ChunkPrinter_h
#define ChunkPrinter_h

#include "PsychicResponse.h"
#ifdef ARDUINO
#include <Print.h>
#else
#include "Print.h" // Using local version support IDF and C++11
#endif

namespace PsychicHttp {

class ChunkPrinter : public Print
{
  private:
    PsychicResponse *_response;
    uint8_t *_buffer;
    size_t _length;
    size_t _pos;

  public:
    ChunkPrinter(PsychicResponse *response, uint8_t *buffer, size_t len);
    ~ChunkPrinter();
  
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    // You can replace this with any custom buffer-read logic if needed
#ifdef ARDUINO
    size_t copyFrom(Stream &stream);
#else
    size_t copyFrom(FILE *stream);
#endif

    void flush() override;
};
} // namespace PsychicHttp
#endif