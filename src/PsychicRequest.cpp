#include "PsychicRequest.h"
#include "http_status.h"
#include "PsychicHttpServer.h"
#include "esp_log.h"

PsychicRequest::PsychicRequest(PsychicHttpServer *server, httpd_req_t *req) :
  _server(server),
  _req(req),
  _method(HTTP_GET),
  _query(""),
  _body(""),
  _tempObject(nullptr)
{
  // load up our client.
  this->_client = server->getClient(req);

  // handle our session data
  if (req->sess_ctx != nullptr)
    this->_session = (SessionData *)req->sess_ctx;
  else
  {
    this->_session = new SessionData();
    req->sess_ctx = this->_session;
  }

  // callback for freeing the session later
  req->free_ctx = this->freeSession;

  // load up some data
  this->_uri = this->_req->uri;
}

PsychicRequest::~PsychicRequest()
{
  // temorary user object
  if (_tempObject != nullptr)
    free(_tempObject);

  // our web parameters
  for (auto *param : _params)
    delete (param);
  _params.clear();
}

void PsychicRequest::freeSession(void *ctx)
{
  if (ctx != nullptr)
  {
    SessionData *session = (SessionData *)ctx;
    delete session;
  }
}

PsychicHttpServer *PsychicRequest::server()
{
  return _server;
}

httpd_req_t *PsychicRequest::request()
{
  return _req;
}

PsychicClient *PsychicRequest::client()
{
  return _client;
}

const char *PsychicRequest::getFilename()
{
  // parse the content-disposition header
  if (this->hasHeader("Content-Disposition"))
  {
    ContentDisposition cd = this->getContentDisposition();
    if (!cd.filename.empty())
     return cd.filename.c_str();
  }

  // fall back to passed in query string
   PsychicWebParameter *param = getParam("_filename");
   if (param != nullptr)
     return param->name().c_str();

  // fall back to parsing it from url (useful for wildcard uploads)
   std::string uri = this->uri();
   auto pos = uri.find_last_of('/');
   std::size_t filenameStart = (pos != std::string::npos ? pos + 1 : 0);
   std::string filename = uri.substr(filenameStart);
   if (!filename.empty()) {
       return filename.c_str();
   }

  // finally, unknown.
  ESP_LOGE(PH_TAG, "Did not get a valid filename from the upload.");
  return "unknown.txt";
}

const ContentDisposition PsychicRequest::getContentDisposition()
{
    ContentDisposition cd;
    std::string header = this->header("Content-Disposition");

    // Set disposition type
    if (header.find("form-data") == 0)
        cd.disposition = FORM_DATA;
    else if (header.find("attachment") == 0)
        cd.disposition = ATTACHMENT;
    else if (header.find("inline") == 0)
        cd.disposition = INLINE;
    else
        cd.disposition = NONE;

    // Parse filename
    size_t start = header.find("filename=");
    if (start != std::string::npos)
    {
        size_t firstQuote = header.find('"', start + 9);
        size_t endQuote = header.find('"', firstQuote + 1);
        if (firstQuote != std::string::npos && endQuote != std::string::npos)
        {
            cd.filename = header.substr(firstQuote + 1, endQuote - firstQuote - 1);
        }
    }

    // Parse name
    start = header.find("name=");
    if (start != std::string::npos)
    {
        size_t firstQuote = header.find('"', start + 5);
        size_t endQuote = header.find('"', firstQuote + 1);
        if (firstQuote != std::string::npos && endQuote != std::string::npos)
        {
            cd.name = header.substr(firstQuote + 1, endQuote - firstQuote - 1);
        }
    }

    return cd;
}

esp_err_t PsychicRequest::loadBody()
{
  esp_err_t err = ESP_OK;

  this->_body.clear();

  size_t remaining = this->_req->content_len;
  size_t actuallyReceived = 0;

  char *buf = static_cast<char *>(malloc(remaining + 1));
  if (buf == nullptr)
  {
    ESP_LOGE(PH_TAG, "Failed to allocate memory for body");
    return ESP_FAIL;
  }

  while (remaining > 0)
  {
    int received = httpd_req_recv(this->_req, buf + actuallyReceived, remaining);

    if (received == HTTPD_SOCK_ERR_TIMEOUT) // -3
    {
      continue;
    }
    else if (received <= HTTPD_SOCK_ERR_FAIL) // include HTTPD_SOCK_ERR_INVALID -2 and  HTTPD_SOCK_ERR_FAIL -1
    {
      ESP_LOGE(PH_TAG, "Failed to receive data.");
      err = ESP_FAIL;
      break;
    }

    remaining -= received;
    actuallyReceived += received;
  }

  buf[actuallyReceived] = '\0';
  this->_body = std::string(buf);
  free(buf);
  return err;
}

http_method PsychicRequest::method() const
{
  return (http_method)_req->method;
}

std::string PsychicRequest::methodStr()
{
  return std::string(http_method_str((http_method)this->_req->method));
}

std::string PsychicRequest::path() {
  const char* q = strchr(_uri, '?');
  if (q == nullptr) {
      return std::string(_uri);
  } else {
      return std::string(_uri, q - _uri);  // construct from pointer + length
  }
}

const char *PsychicRequest::uri() const
{
  return this->_uri;
}

const std::string &PsychicRequest::query() const
{
  return _query;
}

// no way to get list of headers yet....
// int PsychicRequest::headers()
// {
// }

std::string PsychicRequest::header(const char *name)
{
  size_t header_len = httpd_req_get_hdr_value_len(this->_req, name);

  //if we've got one, allocated it and load it
  if (header_len)
  {
    std::string header(header_len + 1, '\0'); // Allocate space, including null terminator
    esp_err_t err = httpd_req_get_hdr_value_str(this->_req, name, &header[0], header_len + 1);

    if (err == ESP_OK)
    {
      header.resize(strnlen(header.c_str(), header_len)); // shrink to real content length
      return header;
    }
  }
  // No such header → return empty string
  return "";
  // return {}; // why empty header like this?
}

bool PsychicRequest::hasHeader(const char *name)
{
  return httpd_req_get_hdr_value_len(this->_req, name) > 0;
}

std::string PsychicRequest::host()
{
  return this->header("Host");
}

std::string PsychicRequest::contentType()
{
  return header("Content-Type");
}

size_t PsychicRequest::contentLength() const
{
  return this->_req->content_len;
}

const std::string& PsychicRequest::body()
{
  return this->_body;
}

bool PsychicRequest::isMultipart()
{
  const std::string &type = this->contentType();
  return type.find("multipart/form-data") != std::string::npos;
}

esp_err_t PsychicRequest::redirect(const char *url)
{
  PsychicResponse response(this);
  response.setCode(301);
  response.addHeader("Location", url);

  return response.send();
}

bool PsychicRequest::hasCookie(const char *key)
{
  char cookie[MAX_COOKIE_SIZE];
  size_t cookieSize = MAX_COOKIE_SIZE;
  esp_err_t err = httpd_req_get_cookie_val(this->_req, key, cookie, &cookieSize);

  // did we get anything?
  if (err == ESP_OK)
    return true;
  else if (err == ESP_ERR_HTTPD_RESULT_TRUNC)
    ESP_LOGE(PH_TAG, "cookie too large (%d bytes).\n", cookieSize);

  return false;
}

std::string PsychicRequest::getCookie(const char *key)
{
  char cookie[MAX_COOKIE_SIZE];
  size_t cookieSize = MAX_COOKIE_SIZE;
  esp_err_t err = httpd_req_get_cookie_val(this->_req, key, cookie, &cookieSize);

  // did we get anything?
  if (err == ESP_OK)
    return std::string(cookie);
  else
    return "";
}

void PsychicRequest::loadParams()
{
    // Check if there's a query string
    size_t query_len = httpd_req_get_url_query_len(_req);
    if (query_len)
    {
        std::string query(query_len + 1, '\0');
        if (httpd_req_get_url_query_str(_req, &query[0], query_len + 1) == ESP_OK)
        {
            _query += query; // Append to _query (make sure _query is a std::string)
            _addParams(_query, false);
        }
    }

    // Check for URL-encoded form data in POST body
    if (this->method() == HTTP_POST && this->contentType().rfind("application/x-www-form-urlencoded", 0) == 0)
    {
        _addParams(_body, true);  // Ensure _body is also a std::string
    }
}

void PsychicRequest::_addParams(const std::string& params, bool post)
{
    size_t start = 0;
    while (start < params.length()) {
        size_t end = params.find('&', start);
        if (end == std::string::npos) end = params.length();

        size_t equal = params.find('=', start);
        if (equal == std::string::npos || equal > end) equal = end;

        std::string name = params.substr(start, equal - start);
        std::string value = (equal + 1 < end) ? params.substr(equal + 1, end - equal - 1) : "";

        addParam(name, value, true, post);

        start = end + 1;
    }
}

PsychicWebParameter* PsychicRequest::addParam(const std::string& name, const std::string& value, bool decode, bool post)
{
    if (decode)
        return addParam(new PsychicWebParameter(urlDecode(name), urlDecode(value), post));
    else
        return addParam(new PsychicWebParameter(name, value, post));
}

PsychicWebParameter* PsychicRequest::addParam(PsychicWebParameter* param)
{
    // ESP_LOGD(PH_TAG, "Adding param: '%s' = '%s'", param->name().c_str(), param->value().c_str());
    _params.push_back(param);
    return param;
}

bool PsychicRequest::hasParam(const char *key)
{
  return getParam(key) != nullptr;
}

PsychicWebParameter* PsychicRequest::getParam(const char* key)
{
    for (auto* param : _params)
    {
        if (param->name() == key)
            return param;
    }
    return nullptr;
}

bool PsychicRequest::hasSessionKey(const char *key)
{
  return this->_session->find(key) != this->_session->end();
}

const std::string PsychicRequest::getSessionKey(const std::string& key)
{
    auto it = this->_session->find(key);
    if (it != this->_session->end())
        return it->second;
    else
        return "";
}

void PsychicRequest::setSessionKey(const std::string &key, const std::string &value)
{
  this->_session->insert(std::pair<std::string, std::string>(key, value));
}

std::string md5str(const std::string& input)
{
#if defined(ARDUINO)
    String in = String(input.c_str());
    MD5Builder md5 = MD5Builder();
    md5.begin();
    md5.add(in);
    md5.calculate();
    in = md5.toString();
    return std::string(in.c_str());
#else
    unsigned char digest[16];  // MD5 produces 128 bits (16 bytes)
    char hex_output[33];       // 2 chars per byte + null terminator

    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, reinterpret_cast<const unsigned char *>(input.c_str()), input.length());
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);

    // Convert digest to hex string (lowercase)
    for (int i = 0; i < 16; ++i) {
        snprintf(hex_output + i * 2, 3, "%02x", digest[i]);
    }

    return std::string(hex_output);
#endif
}

/**
 * @brief  Base64-encode raw bytes.
 * @param  input     pointer to raw input bytes
 * @param  inputLen  number of bytes in input
 * @param  output    buffer to receive Base64 text
 * @param  outputSize size of the output buffer
 * @return >=0 length of the encoded text (not including terminating '\\0'),
 *         or -1 on error (e.g. buffer too small or encode failure)
 */
static int encodeBase64(const char *input, int inputLen, char *output, int outputSize) {
#if defined(ARDUINO)
    int ret = base64_encode_chars(input, inputLen, output);
    if (ret <= 0 || ret + 1 > outputSize) {
        return -1;
    }
    output[ret] = '\0';
    return ret;
#else
    size_t encodedLen = 0;
    int ret = mbedtls_base64_encode(
        reinterpret_cast<unsigned char *>(output),
        outputSize,
        &encodedLen,
        reinterpret_cast<const unsigned char *>(input),
        inputLen);
    if (ret != 0 || encodedLen + 1 > static_cast<size_t>(outputSize)) {
        return -1;
    }
    output[encodedLen] = '\0';
    return static_cast<int>(encodedLen);
#endif
}

// Helper: constant-time string compare
static bool equalsConstantTime(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return result == 0;
}

// Helper: trim whitespace from both ends
static void trim(std::string& s) {
    // left trim
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [](unsigned char ch){ return !std::isspace(ch); }));
    // right trim
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
}

// Always-available helper for expected Base64 length
static size_t b64_expected_len(size_t len) {
    return (4 * ((len + 2) / 3));
}

bool PsychicRequest::authenticate(const char *username, const char *password) {
    if (hasHeader("Authorization")) {
        std::string authReq = header("Authorization");
        if (authReq.rfind("Basic", 0) == 0) {
            // Basic authentication
            authReq.erase(0, 6);
            trim(authReq);

            size_t toencodeLen = strlen(username) + strlen(password) + 1;
            char *toencode = new char[toencodeLen + 1];
            if (toencode == nullptr) {
                return false;
            }

            size_t allocLen = b64_expected_len(toencodeLen);
            int outSize = allocLen + 1;
            char *encoded = new char[outSize];
            if (encoded == nullptr) {
                delete[] toencode;
                return false;
            }

            std::sprintf(toencode, "%s:%s", username, password);
            int encLen = encodeBase64(toencode, static_cast<int>(toencodeLen), encoded, outSize);
            if (encLen > 0 && equalsConstantTime(authReq, std::string(encoded, encLen))) {
                delete[] toencode;
                delete[] encoded;
                return true;
            }
            delete[] toencode;
            delete[] encoded;
        } else if (authReq.rfind("Digest", 0) == 0) {
            // Digest authentication
            authReq.erase(0, 7);

            std::string _username = _extractParam(authReq, "username=\"", '\"');
            if (_username.empty() || _username != username) {
                return false;
            }
            // extracting required parameters for RFC 2069 simpler Digest
            std::string _realm = _extractParam(authReq, "realm=\"", '\"');
            std::string _nonce = _extractParam(authReq, "nonce=\"", '\"');
            std::string _uri = _extractParam(authReq, "uri=\"", '\"');
            std::string _resp = _extractParam(authReq, "response=\"", '\"');
            std::string _opaque = _extractParam(authReq, "opaque=\"", '\"');

            if (_realm.empty() || _nonce.empty() || _uri.empty() || _resp.empty() || _opaque.empty()) {
                return false;
            }
            if (_opaque != getSessionKey("opaque") ||
                _nonce != getSessionKey("nonce") ||
                _realm != getSessionKey("realm")) {
                return false;
            }
            // parameters for the RFC 2617 newer Digest
            std::string _nc, _cnonce;
            if (authReq.find("qop=auth") != std::string::npos || authReq.find("qop=\"auth\"") != std::string::npos) {
                _nc = _extractParam(authReq, "nc=", ',');
                _cnonce = _extractParam(authReq, "cnonce=\"", '\"');
            }

            std::string _H1 = md5str(std::string(username) + ":" + _realm + ":" + std::string(password));
            ESP_LOGD(PH_TAG, "Hash of user:realm:pass=%s", _H1.c_str());

            std::string methodPrefix;
            switch (_method) {
                case HTTP_GET:
                    methodPrefix = "GET:";
                    break;
                case HTTP_POST:
                    methodPrefix = "POST:";
                    break;
                case HTTP_PUT:
                    methodPrefix = "PUT:";
                    break;
                case HTTP_DELETE:
                    methodPrefix = "DELETE:";
                    break;
                default:
                    methodPrefix = "GET:";
                    break;
            }
            std::string _H2 = md5str(methodPrefix + _uri);
            ESP_LOGE(PH_TAG, "Hash of GET:uri=%s", _H2.c_str());

            std::string _responsecheck;
            if (authReq.find("qop=auth") != std::string::npos || authReq.find("qop=\"auth\"") != std::string::npos) {
                _responsecheck = md5str(_H1 + ":" + _nonce + ":" + _nc + ":" + _cnonce + ":auth:" + _H2);
            } else {
                _responsecheck = md5str(_H1 + ":" + _nonce + ":" + _H2);
            }
            ESP_LOGE(PH_TAG, "The Proper response=%s", _responsecheck.c_str());

            if (_resp == _responsecheck) {
                return true;
            }
        }
    }
    return false;
}

std::string PsychicRequest::_extractParam(const std::string &authReq, const std::string &param, char delimit)
{
    size_t begin = authReq.find(param);
    if (begin == std::string::npos)
        return "";
    begin += param.length();
    size_t end = authReq.find(delimit, begin);
    if (end == std::string::npos)
        return authReq.substr(begin);
    return authReq.substr(begin, end - begin);
}

const std::string PsychicRequest::_getRandomHexString()
{
  char buffer[33]; // buffer to hold 32 Hex Digit + /0
  int i;
  for (i = 0; i < 4; i++)
  {
    sprintf(buffer + (i * 8), "%08lx", (unsigned long int)esp_random());
  }
  return std::string(buffer);
}

esp_err_t PsychicRequest::requestAuthentication(HTTPAuthMethod mode, const char* realm, const char* authFailMsg)
{
    // Set realm: if empty, use "Login Required"
    if (strcmp(realm, "") == 0)
        this->setSessionKey("realm", "Login Required");
    else
        this->setSessionKey("realm", realm);

    // Create response object
    PsychicResponse response(this);
    std::string authStr;

    // For Basic authentication:
    if (mode == BASIC_AUTH) {
        // Build: "Basic realm=\"<realm>\""
        authStr = "Basic realm=\"" + this->getSessionKey("realm") + "\"";
        response.addHeader("WWW-Authenticate", authStr.c_str());
    }
    // For Digest authentication:
    else {
        // Create new nonce and opaque if they haven't been set yet.
        if (this->getSessionKey("nonce").empty())
            this->setSessionKey("nonce", _getRandomHexString());
        if (this->getSessionKey("opaque").empty())
            this->setSessionKey("opaque", _getRandomHexString());

        // Build: "Digest realm=\"<realm>\", qop=\"auth\", nonce=\"<nonce>\", opaque=\"<opaque>\""
        authStr = "Digest realm=\"" + this->getSessionKey("realm") +
                  "\", qop=\"auth\", nonce=\"" + this->getSessionKey("nonce") +
                  "\", opaque=\"" + this->getSessionKey("opaque") + "\"";
        response.addHeader("WWW-Authenticate", authStr.c_str());
    }

    response.setCode(401);
    response.setContentType("text/html");
    response.setContent(authStr.c_str());
    return response.send();
}

esp_err_t PsychicRequest::reply(int code)
{
  PsychicResponse response(this);

  response.setCode(code);
  response.setContentType("text/plain");
  response.setContent(http_status_reason(code));

  return response.send();
}

esp_err_t PsychicRequest::reply(const char *content)
{
  PsychicResponse response(this);

  response.setCode(200);
  response.setContentType("text/html");
  response.setContent(content);

  return response.send();
}

esp_err_t PsychicRequest::reply(int code, const char *contentType, const char *content)
{
  PsychicResponse response(this);

  response.setCode(code);
  response.setContentType(contentType);
  response.setContent(content);

  return response.send();
}