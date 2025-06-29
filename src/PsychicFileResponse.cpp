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
    // set filename and force download
    snprintf(buf, sizeof(buf), "attachment; filename=\"%s\"", filename);
  }
  else
  {
    // set filename and force rendering
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

bool PsychicFileResponse::_endsWith(const std::string& value, const std::string& ending)
{
  if (ending.size() > value.size()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void PsychicFileResponse::_setContentType(const std::string &path)
{
  const char* contentType;

  if (_endsWith(path, ".html") || _endsWith(path, ".htm"))
    contentType = "text/html";
  else if (_endsWith(path, ".css"))
    contentType = "text/css";
  else if (_endsWith(path, ".json"))
    contentType = "application/json";
  else if (_endsWith(path, ".js"))
    contentType = "application/javascript";
  else if (_endsWith(path, ".png"))
    contentType = "image/png";
  else if (_endsWith(path, ".gif"))
    contentType = "image/gif";
  else if (_endsWith(path, ".jpg"))
    contentType = "image/jpeg";
  else if (_endsWith(path, ".ico"))
    contentType = "image/x-icon";
  else if (_endsWith(path, ".svg"))
    contentType = "image/svg+xml";
  else if (_endsWith(path, ".eot"))
    contentType = "font/eot";
  else if (_endsWith(path, ".woff"))
    contentType = "font/woff";
  else if (_endsWith(path, ".woff2"))
    contentType = "font/woff2";
  else if (_endsWith(path, ".ttf"))
    contentType = "font/ttf";
  else if (_endsWith(path, ".xml"))
    contentType = "text/xml";
  else if (_endsWith(path, ".pdf"))
    contentType = "application/pdf";
  else if (_endsWith(path, ".zip"))
    contentType = "application/zip";
  else if (_endsWith(path, ".gz"))
    contentType = "application/x-gzip";
  else
    contentType = "text/plain";

  setContentType(contentType);
}

esp_err_t PsychicFileResponse::send()
{
    esp_err_t err = ESP_OK;

    //just send small files directly
    size_t size = getContentLength();

    if (size < FILE_CHUNK_SIZE)
    {
        uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
        if (!buffer)
        {
            /* Respond with 500 Internal Server Error */
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
        /* Retrieve the pointer to scratch buffer for temporary storage */
        char *chunk = static_cast<char *>(malloc(FILE_CHUNK_SIZE));
        if (!chunk)
        {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(this->_request->request(), HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to allocate memory.");
            return ESP_FAIL;
        }

        this->sendHeaders();

        size_t chunksize;
        do
        {
		    /* Read file in chunks into the scratch buffer */
            chunksize = fread(chunk, 1, FILE_CHUNK_SIZE, _content);
            if (chunksize > 0)
            {
                err = this->sendChunk(reinterpret_cast<uint8_t *>(chunk), chunksize);
                if (err != ESP_OK)
                    break;
            }

        /* Keep looping till the whole file is sent */
        } while (chunksize != 0 && !feof(_content));

        //keep track of our memory
        free(chunk);

        if (err == ESP_OK)
        {
            ESP_LOGD(PH_TAG, "File sending complete");
            this->finishChunking();
        }
    }

    return err;
}