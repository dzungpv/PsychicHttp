#include "PsychicStaticFileHandler.h"

/*************************************/
/*  PsychicStaticFileHandler         */
/*************************************/

PsychicStaticFileHandler::PsychicStaticFileHandler(const char* uri, const char* path, const char* cache_control)
  : _uri(uri), _path(path), _cache_control(cache_control ? cache_control : ""), _default_file("index.html"), _last_modified(""), _file(nullptr)
{
// Ensure leading '/'
  if (_uri.empty() || _uri[0] != '/') _uri = "/" + _uri;
  if (_path.empty() || _path[0] != '/') _path = "/" + _path;

  // If path ends with '/' we assume a hint that this is a directory to improve performance.
  // However - if it does not end with '/' we, can't assume a file, path can still be a directory.
  _isDir = !_path.empty() && _path.back() == '/';
  
  // Remove the trailing '/' so we can handle default file
  // Notice that root will be "" not "/"
  if (!_uri.empty() && _uri.back() == '/') _uri.pop_back();
  if (!_path.empty() && _path.back() == '/') _path.pop_back();

  // Reset stats
  _gzipFirst = false;
  _gzipStats = 0xF8;
}

PsychicStaticFileHandler& PsychicStaticFileHandler::setIsDir(bool isDir) {
  _isDir = isDir;
  return *this;
}

PsychicStaticFileHandler& PsychicStaticFileHandler::setDefaultFile(const char* filename) {
  _default_file = filename;
  return *this;
}

PsychicStaticFileHandler& PsychicStaticFileHandler::setCacheControl(const char* cache_control) {
  _cache_control = cache_control;
  return *this;
}

PsychicStaticFileHandler& PsychicStaticFileHandler::setLastModified(const char* last_modified) {
  _last_modified = last_modified;
  return *this;
}

PsychicStaticFileHandler& PsychicStaticFileHandler::setLastModified(struct tm* last_modified) {
  char buffer[64];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", last_modified);
  _last_modified = buffer;
  return *this;
}

bool PsychicStaticFileHandler::canHandle(PsychicRequest *request)
{
  if (request->method() != HTTP_GET)
    return false;

  const char* reqUri = request->uri();
  size_t prefixLen = _uri.length();

  // Check if request URI starts with _uri
  if (strncmp(reqUri, _uri.c_str(), prefixLen) != 0)
    return false;

  return _getFile(request);
}

bool PsychicStaticFileHandler::_getFile(PsychicRequest *request)
{
  std::string path = request->uri();
  if (path.rfind(_uri, 0) == 0) {
    path = path.substr(_uri.length());  // Remove the matched prefix
  }

  // We can skip the file check and look for default if request is to the root of a directory or that request path ends with '/'
  bool canSkipFileCheck = (_isDir && path.empty()) || (!path.empty() && path.back() == '/');

  path = _path + path;

  if (!canSkipFileCheck && _fileExists(path)) {
    return true;
  }

  // Can't handle if not default file
  if (_default_file.empty()) {
    return false;
  }

  // Try to add default file, ensure there is a trailing '/' ot the path.
  if (path.empty() || path.back() != '/') {
    path += "/";
  }
  path += _default_file;

  return _fileExists(path);
}

bool PsychicStaticFileHandler::_fileExists(const std::string& path) {
  bool fileFound = false;
  bool gzipFound = false;
  std::string gzip = path + ".gz";

  if (_gzipFirst) {
    _file = fopen(gzip.c_str(), "rb");
    gzipFound = (_file != nullptr);
    if (!gzipFound) {
      _file = fopen(path.c_str(), "rb");
      fileFound = (_file != nullptr);
    }
  } else {
    _file = fopen(path.c_str(), "rb");
    fileFound = (_file != nullptr);
    if (!fileFound) {
      _file = fopen(gzip.c_str(), "rb");
      gzipFound = (_file != nullptr);
    }
  }

  bool found = fileFound || gzipFound;

  if (found) {
    _filename = fileFound ? path : gzip;

    // Calculate gzip statistic
    _gzipStats = (_gzipStats << 1) + (gzipFound ? 1 : 0);
    if (_gzipStats == 0x00) _gzipFirst = false; // All files are not gzip
    else if (_gzipStats == 0xFF) _gzipFirst = true; // All files are gzip
    else _gzipFirst = _countBits(_gzipStats) > 4; // If we have more gzip files - try gzip first
  }

  return found;
}

uint8_t PsychicStaticFileHandler::_countBits(const uint8_t value) const
{
  uint8_t w = value;
  uint8_t n;
  for (n=0; w!=0; n++) w&=w-1;
  return n;
}

size_t PsychicStaticFileHandler::_getFileSize(FILE* f) {
  if (!f) return 0;

  long current = ftell(f);
  if (current < 0) return 0;

  if (fseek(f, 0, SEEK_END) != 0) return 0;
  long size = ftell(f);
  fseek(f, current, SEEK_SET);

  return (size >= 0) ? static_cast<size_t>(size) : 0;
}

esp_err_t PsychicStaticFileHandler::handleRequest(PsychicRequest *request) {
  if (_file) {
    //is it not modified?
    std::string etag = std::to_string(_getFileSize(_file));

    if (!_last_modified.empty() && _last_modified == request->header("If-Modified-Since")) {
      fclose(_file);
      _file = nullptr;
      request->reply(304); // Not modified
    }
    //does our Etag match?
    else if (!_cache_control.empty() && request->hasHeader("If-None-Match") && request->header("If-None-Match") == etag) {
      fclose(_file);
      _file = nullptr;

      PsychicResponse response(request);
      response.addHeader("Cache-Control", _cache_control.c_str());
      response.addHeader("ETag", etag.c_str());
      response.setCode(304);
      response.send();
    }
    //nope, send them the full file.
    else {
      PsychicFileResponse response(request, _filename.c_str());

      if (!_last_modified.empty())
        response.addHeader("Last-Modified", _last_modified.c_str());
      if (!_cache_control.empty()) {
        response.addHeader("Cache-Control", _cache_control.c_str());
        response.addHeader("ETag", etag.c_str());
      }

      return response.send();
    }
  } else {
    return request->reply(404);
  }

  return ESP_OK;
}