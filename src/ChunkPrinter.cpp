#include "ChunkPrinter.h"
#include <cstring>
#include <esp_log.h>

ChunkPrinter::ChunkPrinter(PsychicResponse* response, uint8_t* buffer, size_t len)
    : _response(response), _buffer(buffer), _length(len), _pos(0) {}

ChunkPrinter::~ChunkPrinter() {
    flush();
}

size_t ChunkPrinter::write(uint8_t c) {
    if (_pos == _length) {
        if (_response->sendChunk(_buffer, _length) != ESP_OK) return 0;
        _pos = 0;
    }

    _buffer[_pos++] = c;
    return 1;
}

size_t ChunkPrinter::write(const uint8_t* buffer, size_t size) {
    size_t written = 0;

    while (written < size) {
        size_t space = _length - _pos;
        size_t blockSize = (space < (size - written)) ? space : (size - written);

        memcpy(_buffer + _pos, buffer + written, blockSize);
        _pos += blockSize;
        written += blockSize;

        if (_pos == _length) {
            if (_response->sendChunk(_buffer, _length) != ESP_OK) return written;
            _pos = 0;
        }
    }

    return written;
}

void ChunkPrinter::flush() {
    if (_pos > 0) {
        _response->sendChunk(_buffer, _pos);
        _pos = 0;
    }
}

size_t ChunkPrinter::copyFrom(FILE* stream) {
    size_t count = 0;

    while (!feof(stream)) {
        if (_pos == _length) {
            _response->sendChunk(_buffer, _length);
            _pos = 0;
        }

        size_t readBytes = fread(_buffer + _pos, 1, _length - _pos, stream);
        _pos += readBytes;
        count += readBytes;

        if (readBytes == 0) break;
    }

    return count;
}