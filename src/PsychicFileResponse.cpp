#include "PsychicFileResponse.h"
#include "PsychicResponse.h"
#include "PsychicRequest.h"
#include "esp_log.h"

PsychicFileResponse::PsychicFileResponse(PsychicRequest *request, const std::string &path, const std::string &contentType, bool download)
    : PsychicResponse(request)
{

  std::string _path = path;

  // Try gzip if requested file doesn't exist and gzip version exists
  if (!download && access(_path.c_str(), F_OK) != 0 && access((_path + ".gz").c_str(), F_OK) == 0)
  {
    _path += ".gz";
    addHeader("Content-Encoding", "gzip");
  }

  // Open the file with fopen
  _content = fopen(_path.c_str(), "r");
  if (!_content)
  {
    // handle file open error here
    return;
  }

  // Determine file size
  fseek(_content, 0, SEEK_END);
  _contentLength = ftell(_content);
  fseek(_content, 0, SEEK_SET);

  // Detect content type if not explicitly set
  if (contentType.empty())
    _setContentType(path);
  else
    setContentType(contentType.c_str());

  // Extract filename from path
  size_t filenameStart = path.find_last_of('/');
  if (filenameStart == std::string::npos)
    filenameStart = 0;
  else
    filenameStart += 1;

  const char *filename = path.c_str() + filenameStart;

  // Build Content-Disposition header
  char buf[256];
  if (download)
  {
    snprintf(buf, sizeof(buf), "attachment; filename=\"%s\"", filename);
  }
  else
  {
    snprintf(buf, sizeof(buf), "inline; filename=\"%s\"", filename);
  }
  addHeader("Content-Disposition", buf);
}

PsychicFileResponse::PsychicFileResponse(PsychicRequest *request, FILE* content, const std::string &path, const std::string &contentType, bool download)
    : PsychicResponse(request)
{

  std::string _path = path;

// Extract filename from content path (assumed to be provided separately)
std::string filename = path;  // 👈 replace with actual filename or file path string

// Add gzip header if needed
if (!download &&
    filename.size() >= 3 &&
    filename.compare(filename.size() - 3, 3, ".gz") == 0 &&
    (_path.size() < 3 || _path.compare(_path.size() - 3, 3, ".gz") != 0))
{
    addHeader("Content-Encoding", "gzip");
}

// Open the file
_content = fopen(path.c_str(), "r");
if (!_content) {
    // handle open failure
    return;
}

// Get file size
fseek(_content, 0, SEEK_END);
_contentLength = ftell(_content);
fseek(_content, 0, SEEK_SET);

// Set content type
if (contentType.empty())
    _setContentType(path);
else
    setContentType(contentType.c_str());

// Extract filename from path (for Content-Disposition)
size_t filenameStart = path.find_last_of('/');
if (filenameStart == std::string::npos)
    filenameStart = 0;
else
    filenameStart += 1;

const char* fname = path.c_str() + filenameStart;

// Set Content-Disposition header
char buf[256];
if (download) {
    snprintf(buf, sizeof(buf), "attachment; filename=\"%s\"", fname);
} else {
    snprintf(buf, sizeof(buf), "inline; filename=\"%s\"", fname);
}
addHeader("Content-Disposition", buf);
}

PsychicFileResponse::~PsychicFileResponse()
{
  if (_content)
    fclose(_content);
}

bool endsWith(const std::string& value, const std::string& ending)
{
  if (ending.size() > value.size()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void PsychicFileResponse::_setContentType(const std::string &path)
{
  const char* contentType;

  if (path.ends_with(".html") || path.ends_with(".htm"))
    contentType = "text/html";
  else if (path.ends_with(".css"))
    contentType = "text/css";
  else if (path.ends_with(".json"))
    contentType = "application/json";
  else if (path.ends_with(".js"))
    contentType = "application/javascript";
  else if (path.ends_with(".png"))
    contentType = "image/png";
  else if (path.ends_with(".gif"))
    contentType = "image/gif";
  else if (path.ends_with(".jpg"))
    contentType = "image/jpeg";
  else if (path.ends_with(".ico"))
    contentType = "image/x-icon";
  else if (path.ends_with(".svg"))
    contentType = "image/svg+xml";
  else if (path.ends_with(".eot"))
    contentType = "font/eot";
  else if (path.ends_with(".woff"))
    contentType = "font/woff";
  else if (path.ends_with(".woff2"))
    contentType = "font/woff2";
  else if (path.ends_with(".ttf"))
    contentType = "font/ttf";
  else if (path.ends_with(".xml"))
    contentType = "text/xml";
  else if (path.ends_with(".pdf"))
    contentType = "application/pdf";
  else if (path.ends_with(".zip"))
    contentType = "application/zip";
  else if (path.ends_with(".gz"))
    contentType = "application/x-gzip";
  else
    contentType = "text/plain";

  setContentType(contentType);
}

esp_err_t PsychicFileResponse::send()
{
    esp_err_t err = ESP_OK;

    size_t size = getContentLength();

    if (size < FILE_CHUNK_SIZE)
    {
        uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
        if (!buffer)
        {
            httpd_resp_send_err(this->_request->request(), HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to allocate memory.");
            return ESP_FAIL;
        }

        size_t readSize = fread(buffer, 1, size, _content);  // _content is assumed to be FILE*

        this->setContent(buffer, readSize);
        err = PsychicResponse::send();

        free(buffer);
    }
    else
    {
        char *chunk = static_cast<char *>(malloc(FILE_CHUNK_SIZE));
        if (!chunk)
        {
            httpd_resp_send_err(this->_request->request(), HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to allocate memory.");
            return ESP_FAIL;
        }

        this->sendHeaders();

        size_t chunksize;
        do
        {
            chunksize = fread(chunk, 1, FILE_CHUNK_SIZE, _content);
            if (chunksize > 0)
            {
                err = this->sendChunk(reinterpret_cast<uint8_t *>(chunk), chunksize);
                if (err != ESP_OK)
                    break;
            }

        } while (chunksize != 0 && !feof(_content));

        free(chunk);

        if (err == ESP_OK)
        {
            ESP_LOGD(PH_TAG, "File sending complete");
            this->finishChunking();
        }
    }

    return err;
}