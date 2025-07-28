#include "PsychicFileResponse.h"
#include "PsychicRequest.h"
#include "PsychicResponse.h"
#include <string>
#include <algorithm>
#include "esp_log.h"

namespace PsychicHttp {

PsychicFileResponse::PsychicFileResponse(PsychicResponse* response, const std::string& path, const std::string& contentType, bool download) : PsychicResponseDelegate(response)
{
  //_code = 200;
  std::string _path(path);

  if (!download && access(_path.c_str(), F_OK) != 0 && access((_path + ".gz").c_str(), F_OK) == 0) {
    _path = _path + ".gz";
    addHeader("Content-Encoding", "gzip");
  }

  _content = fopen(_path.c_str(), "r");
  if (_content) {
    fseek(_content, 0, SEEK_END);
    long fileSize = ftell(_content);
    fseek(_content, 0, SEEK_SET);
    setContentLength(fileSize);
  }

  if (contentType == "")
    _setContentTypeFromPath(path);
  else
    setContentType(contentType.c_str());

  size_t filenameStart = path.find_last_of('/') + 1;
  char buf[26 + path.length() - filenameStart];
  const char* filename = path.c_str() + filenameStart;

  if (download) {
    // set filename and force download
    snprintf(buf, sizeof(buf), "attachment; filename=\"%s\"", filename);
  } else {
    // set filename and force rendering
    snprintf(buf, sizeof(buf), "inline; filename=\"%s\"", filename);
  }
  addHeader("Content-Disposition", buf);
}

PsychicFileResponse::PsychicFileResponse(PsychicResponse* response, FILE* content, const std::string& path, const std::string& contentType, bool download) : PsychicResponseDelegate(response)
{
  std::string _path(path);

  if (!download && endsWith(path, ".gz") && !endsWith(path, ".gz")) {
    addHeader("Content-Encoding", "gzip");
  }

  _content = content;
  if (_content) {
    fseek(_content, 0, SEEK_END);
    long fileSize = ftell(_content);
    fseek(_content, 0, SEEK_SET);
    setContentLength(fileSize);
  }

  if (contentType == "")
    _setContentTypeFromPath(path);
  else
    setContentType(contentType.c_str());

  size_t filenameStart = path.find_last_of('/') + 1;
  char buf[26 + path.length() - filenameStart];
  const char* filename = path.c_str() + filenameStart;

  if (download) {
    snprintf(buf, sizeof(buf), "attachment; filename=\"%s\"", filename);
  } else {
    snprintf(buf, sizeof(buf), "inline; filename=\"%s\"", filename);
  }
  addHeader("Content-Disposition", buf);
}

PsychicFileResponse::~PsychicFileResponse()
{
  if (_content)
    fclose(_content);
}

bool PsychicFileResponse::endsWith(const std::string& value, const std::string& ending)
{
  if (ending.size() > value.size()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void PsychicFileResponse::_setContentTypeFromPath(const std::string& path)
{
  const char* _contentType;

  if (endsWith(path, ".html"))
    _contentType = "text/html";
  else if (endsWith(path, ".htm"))
    _contentType = "text/html";
  else if (endsWith(path, ".css"))
    _contentType = "text/css";
  else if (endsWith(path, ".json"))
    _contentType = "application/json";
  else if (endsWith(path, ".js"))
    _contentType = "application/javascript";
  else if (endsWith(path, ".png"))
    _contentType = "image/png";
  else if (endsWith(path, ".gif"))
    _contentType = "image/gif";
  else if (endsWith(path, ".jpg"))
    _contentType = "image/jpeg";
  else if (endsWith(path, ".ico"))
    _contentType = "image/x-icon";
  else if (endsWith(path, ".svg"))
    _contentType = "image/svg+xml";
  else if (endsWith(path, ".eot"))
    _contentType = "font/eot";
  else if (endsWith(path, ".woff"))
    _contentType = "font/woff";
  else if (endsWith(path, ".woff2"))
    _contentType = "font/woff2";
  else if (endsWith(path, ".ttf"))
    _contentType = "font/ttf";
  else if (endsWith(path, ".xml"))
    _contentType = "text/xml";
  else if (endsWith(path, ".pdf"))
    _contentType = "application/pdf";
  else if (endsWith(path, ".zip"))
    _contentType = "application/zip";
  else if (endsWith(path, ".gz"))
    _contentType = "application/x-gzip";
  else
    _contentType = "text/plain";

  setContentType(_contentType);
}

esp_err_t PsychicFileResponse::send()
{
  esp_err_t err = ESP_OK;

  // just send small files directly
  size_t size = getContentLength();
  if (size < FILE_CHUNK_SIZE) {
    uint8_t* buffer = (uint8_t*)malloc(size);
    if (buffer == NULL) {
      ESP_LOGE(PH_TAG, "Unable to allocate %d bytes to send chunk", size);
      httpd_resp_send_err(request(), HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to allocate memory.");
      return ESP_FAIL;
    }

    size_t readSize = fread(buffer, 1, size, _content);  // _content is assumed to be FILE*

    setContent(buffer, readSize);
    err = _response->send();

    free(buffer);
  } else {
    /* Retrieve the pointer to scratch buffer for temporary storage */
    char* chunk = (char*)malloc(FILE_CHUNK_SIZE);
    if (chunk == NULL) {
      ESP_LOGE(PH_TAG, "Unable to allocate %d bytes to send chunk", FILE_CHUNK_SIZE);
      httpd_resp_send_err(request(), HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to allocate memory.");
      return ESP_FAIL;
    }

    sendHeaders();

    size_t chunksize;
    do {
      /* Read file in chunks into the scratch buffer */
      chunksize = fread(chunk, 1, FILE_CHUNK_SIZE, _content);
      if (chunksize > 0) {
        err = sendChunk((uint8_t*)chunk, chunksize);
        if (err != ESP_OK)
          break;
      }

      /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    // keep track of our memory
    free(chunk);

    if (err == ESP_OK) {
      ESP_LOGD(PH_TAG, "File sending complete");
      finishChunking();
    }
  }

  return err;
}

} // namespace PsychicHttp
