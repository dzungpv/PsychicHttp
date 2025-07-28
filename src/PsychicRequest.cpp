#include "PsychicRequest.h"
#include "MultipartProcessor.h"
#include "PsychicHttpServer.h"
#include "http_status.h"
#include <string>
#include "esp_log.h"

namespace PsychicHttp {

PsychicRequest::PsychicRequest(PsychicHttpServer* server, httpd_req_t* req) : _server(server),
                                                                              _req(req),
                                                                              _endpoint(nullptr),
                                                                              _method(HTTP_GET),
                                                                              _uri(""),
                                                                              _query(""),
                                                                              _body(""),
                                                                              _tempObject(nullptr)
{
  // load up our client.
  this->_client = server->getClient(req);

  // handle our session data
  if (req->sess_ctx != NULL)
    this->_session = (SessionData*)req->sess_ctx;
  else {
    this->_session = new SessionData();
    req->sess_ctx = this->_session;
  }

  // callback for freeing the session later
  req->free_ctx = this->freeSession;

  // load and parse our uri.
  this->_setUri(this->_req->uri);

  _response = new PsychicResponse(this);
}

PsychicRequest::~PsychicRequest()
{
  // temorary user object
  if (_tempObject != NULL)
    free(_tempObject);

  // our web parameters
  for (auto* param : _params)
    delete (param);
  _params.clear();

  delete _response;
}

void PsychicRequest::freeSession(void* ctx)
{
  if (ctx != NULL) {
    SessionData* session = (SessionData*)ctx;
    delete session;
  }
}

PsychicHttpServer* PsychicRequest::server()
{
  return _server;
}

httpd_req_t* PsychicRequest::request()
{
  return _req;
}

PsychicClient* PsychicRequest::client()
{
  return _client;
}

PsychicEndpoint* PsychicRequest::endpoint()
{
  return _endpoint;
}

void PsychicRequest::setEndpoint(PsychicEndpoint* endpoint)
{
  _endpoint = endpoint;
}

#ifdef PSY_ENABLE_REGEX
bool PsychicRequest::getRegexMatches(std::smatch& matches, bool use_full_uri)
{
  if (_endpoint != nullptr) {
    std::regex pattern(_endpoint->uri().c_str());
    std::string s(this->path().c_str());
    if (use_full_uri)
      s = this->uri().c_str();

    return std::regex_search(s, matches, pattern);
  }

  return false;
}
#endif

const std::string PsychicRequest::getFilename()
{
  // parse the content-disposition header
  if (this->hasHeader("Content-Disposition")) {
    ContentDisposition cd = this->getContentDisposition();
    if (cd.filename != "")
      return cd.filename;
  }

  // fall back to passed in query string
  PsychicWebParameter* param = getParam("_filename");
  if (param != NULL)
    return param->name();

  // fall back to parsing it from url (useful for wildcard uploads)
  std::string uri = this->uri();
  int filenameStart = uri.find_last_of('/') + 1;
  std::string filename = uri.substr(filenameStart);
  if (filename != "")
    return filename;

  // finally, unknown.
  ESP_LOGE(PH_TAG, "Did not get a valid filename from the upload.");
  return "unknown.txt";
}

const ContentDisposition PsychicRequest::getContentDisposition()
{
  ContentDisposition cd;
  std::string header = this->header("Content-Disposition");
  int start;
  int end;

  if (header.find("form-data") == 0)
    cd.disposition = FORM_DATA;
  else if (header.find("attachment") == 0)
    cd.disposition = ATTACHMENT;
  else if (header.find("inline") == 0)
    cd.disposition = INLINE;
  else
    cd.disposition = NONE;

  start = header.find("filename=");
  if (start) {
    end = header.find('"', start + 10);
    cd.filename = header.substr(start + 10, end - 1);
  }

  start = header.find("name=");
  if (start) {
    end = header.find('"', start + 6);
    cd.name = header.substr(start + 6, end - 1);
  }

  return cd;
}

esp_err_t PsychicRequest::loadBody()
{
  if (_bodyParsed != ESP_ERR_NOT_FINISHED)
    return _bodyParsed;

  // quick size check.
  if (contentLength() > server()->maxRequestBodySize) {
    ESP_LOGE(PH_TAG, "Body size larger than maxRequestBodySize");
    return _bodyParsed = ESP_ERR_INVALID_SIZE;
  }

  this->_body = std::string();

  size_t remaining = this->_req->content_len;
  size_t actuallyReceived = 0;
  char* buf = (char*)malloc(remaining + 1);
  if (buf == NULL) {
    ESP_LOGE(PH_TAG, "Failed to allocate memory for body");
    return _bodyParsed = ESP_FAIL;
  }

  while (remaining > 0) {
    int received = httpd_req_recv(this->_req, buf + actuallyReceived, remaining);

    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    } else if (received == HTTPD_SOCK_ERR_FAIL) {
      ESP_LOGE(PH_TAG, "Failed to receive data.");
      _bodyParsed = ESP_FAIL;
      break;
    }

    remaining -= received;
    actuallyReceived += received;
  }

  buf[actuallyReceived] = '\0';
  this->_body = std::string(buf);
  free(buf);

  _bodyParsed = ESP_OK;

  return _bodyParsed;
}

http_method PsychicRequest::method()
{
  return (http_method)this->_req->method;
}

const std::string PsychicRequest::methodStr()
{
  return std::string(http_method_str((http_method)this->_req->method));
}

const std::string PsychicRequest::path()
{
  size_t index = _uri.find("?");
  if (index == std::string::npos)
    return _uri;
  else
    return _uri.substr(0, index);
}

const std::string& PsychicRequest::uri()
{
  return this->_uri;
}

const std::string& PsychicRequest::query()
{
  return this->_query;
}

// no way to get list of headers yet....
// int PsychicRequest::headers()
// {
// }

const std::string PsychicRequest::header(const char* name)
{
  size_t header_len = httpd_req_get_hdr_value_len(this->_req, name);

  // if we've got one, allocated it and load it
  if (header_len) {
    char header[header_len + 1];
    httpd_req_get_hdr_value_str(this->_req, name, header, sizeof(header));
    return std::string(header);
  } else
    return "";
}

bool PsychicRequest::hasHeader(const char* name)
{
  return httpd_req_get_hdr_value_len(this->_req, name) > 0;
}

const std::string PsychicRequest::host()
{
  return this->header("Host");
}

const std::string PsychicRequest::contentType()
{
  return header("Content-Type");
}

size_t PsychicRequest::contentLength()
{
  return this->_req->content_len;
}

const std::string& PsychicRequest::body()
{
  return this->_body;
}

bool PsychicRequest::isMultipart()
{
  const std::string& type = this->contentType();

  return (this->contentType().find("multipart/form-data") != std::string::npos);
}

bool PsychicRequest::hasCookie(const char* key, size_t* size)
{
  char buffer;

  // this keeps our size for the user.
  if (size != nullptr) {
    *size = 1;
    return getCookie(key, &buffer, size) != ESP_ERR_NOT_FOUND;
  }
  // this just checks that it exists.
  else {
    size_t mysize = 1;
    return getCookie(key, &buffer, &mysize) != ESP_ERR_NOT_FOUND;
  }
}

esp_err_t PsychicRequest::getCookie(const char* key, char* buffer, size_t* size)
{
  return httpd_req_get_cookie_val(this->_req, key, buffer, size);
}

std::string PsychicRequest::getCookie(const char* key)
{
  std::string cookie = "";

  // how big is our cookie?
  size_t size;
  if (!hasCookie("counter", &size))
    return cookie;

  // allocate cookie buffer... keep it on the stack
  char buf[size];

  // load it up.
  esp_err_t err = getCookie(key, buf, &size);
  if (err == ESP_OK)
    cookie.append(buf);

  return cookie;
}

void PsychicRequest::replaceResponse(PsychicResponse* response)
{
  delete _response;
  _response = response;
}

void PsychicRequest::addResponseHeader(const char* key, const char* value)
{
  _response->addHeader(key, value);
}

std::list<HTTPHeader>& PsychicRequest::getResponseHeaders()
{
  return _response->headers();
}

void PsychicRequest::loadParams()
{
  if (_paramsParsed != ESP_ERR_NOT_FINISHED)
    return;

  // convenience shortcut to allow calling loadParams()
  if (_bodyParsed == ESP_ERR_NOT_FINISHED)
    loadBody();

  // various form data as parameters
  if (this->method() == HTTP_POST) {
    if (this->contentType().find("application/x-www-form-urlencoded") == 0)
      _addParams(_body, true);

    if (this->isMultipart()) {
      MultipartProcessor mpp(this);
      _paramsParsed = mpp.process(_body.c_str());
      return;
    }
  }

  _paramsParsed = ESP_OK;
}

void PsychicRequest::_setUri(const char* uri)
{
  // save it
  _uri = std::string(uri);

  // look for our query separator
  size_t index = _uri.find('?', 0);
  if (index != std::string::npos) {
    // parse them.
    _query = _uri.substr(index + 1);
    _addParams(_query, false);
  }
}

void PsychicRequest::_addParams(const std::string& params, bool post)
{
  size_t start = 0;
  while (start < params.length()) {
    size_t end = params.find('&', start);
    if (end == std::string::npos)
      end = params.length();
    size_t equal = params.find('=', start);
    if (equal == std::string::npos || equal > end)
      equal = end;
    std::string name = params.substr(start, equal - start);
    std::string value = equal + 1 < end ? params.substr(equal + 1, end - (equal + 1)) : std::string();
    addParam(name, value, true, post);
    start = end + 1;
  }
}

PsychicWebParameter* PsychicRequest::addParam(const std::string& name, const std::string& value, bool decode, bool post)
{
  if (decode)
    return addParam(new PsychicWebParameter(urlDecode(name.c_str()), urlDecode(value.c_str()), post));
  else
    return addParam(new PsychicWebParameter(name, value, post));
}

PsychicWebParameter* PsychicRequest::addParam(PsychicWebParameter* param)
{
  // ESP_LOGD(PH_TAG, "Adding param: '%s' = '%s'", param->name().c_str(), param->value().c_str());
  _params.push_back(param);
  return param;
}

bool PsychicRequest::hasParam(const char* key)
{
  return getParam(key) != NULL;
}

bool PsychicRequest::hasParam(const char* key, bool isPost, bool isFile)
{
  return getParam(key, isPost, isFile) != NULL;
}

PsychicWebParameter* PsychicRequest::getParam(const char* key)
{
  for (auto* param : _params)
    if (param->name() == key)
      return param;

  return NULL;
}

PsychicWebParameter* PsychicRequest::getParam(const char* key, bool isPost, bool isFile)
{
  for (auto* param : _params)
    if (param->name() == key && isPost == param->isPost() && isFile == param->isFile())
      return param;
  return NULL;
}

bool PsychicRequest::hasSessionKey(const std::string& key)
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

void PsychicRequest::setSessionKey(const std::string& key, const std::string& value)
{
  this->_session->insert(std::pair<std::string, std::string>(key, value));
}

std::string md5str(const std::string& input)
{
#ifdef ARDUINO
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

// Always-available helper for expected Base64 length
static size_t b64_expected_len(size_t len)
{
  return (4 * ((len + 2) / 3));
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
static int encodeBase64(const char* input, int inputLen, char* output, int outputSize)
{
#ifdef ARDUINO
  int ret = base64_encode_chars(input, inputLen, output);
  if (ret <= 0 || ret + 1 > outputSize) {
    return -1;
  }
  output[ret] = '\0';
  return ret;
#else
  size_t encodedLen = 0;
  int ret = mbedtls_base64_encode(
    reinterpret_cast<unsigned char*>(output),
    outputSize,
    &encodedLen,
    reinterpret_cast<const unsigned char*>(input),
    inputLen);
  if (ret != 0 || encodedLen + 1 > static_cast<size_t>(outputSize)) {
    return -1;
  }
  output[encodedLen] = '\0';
  return static_cast<int>(encodedLen);
#endif
}

// Helper: constant-time string compare
static bool equalsConstantTime(const std::string& a, const std::string& b)
{
  if (a.size() != b.size())
    return false;
  unsigned char result = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  }
  return result == 0;
}

// Helper: trim whitespace from both ends
static void trim(std::string& s)
{
  // left trim
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  // right trim
  s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
}

bool PsychicRequest::authenticate(const char* username, const char* password)
{
  if (hasHeader("Authorization")) {
    std::string authReq = header("Authorization");
    if (authReq.find("Basic") == 0) {
      authReq = authReq.substr(6);
      // trim whitespace
      authReq.erase(0, authReq.find_first_not_of(" \t\r\n"));
      authReq.erase(authReq.find_last_not_of(" \t\r\n") + 1);
      char toencodeLen = strlen(username) + strlen(password) + 1;
      char* toencode = new char[toencodeLen + 1];
      if (toencode == NULL) {
        authReq = "";
        return false;
      }
      size_t allocLen = b64_expected_len(toencodeLen);
      int outSize = allocLen + 1;
      char *encoded = new char[outSize];
      if (encoded == NULL) {
        authReq = "";
        delete[] toencode;
        return false;
      }
      sprintf(toencode, "%s:%s", username, password);
      int encLen = encodeBase64(toencode, static_cast<int>(toencodeLen), encoded, outSize);
      if (encLen > 0 && equalsConstantTime(authReq, std::string(encoded, encLen))) {
        authReq = "";
        delete[] toencode;
        delete[] encoded;
        return true;
      }
      delete[] toencode;
      delete[] encoded;
    } else if (authReq.find("Digest") == 0) {
      authReq = authReq.substr(7);
      std::string _username = _extractParam(authReq, "username=\"", '"');
      if (!_username.length() || _username != std::string(username)) {
        authReq = "";
        return false;
      }
      // extracting required parameters for RFC 2069 simpler Digest
      std::string _realm = _extractParam(authReq, "realm=\"", '"');
      std::string _nonce = _extractParam(authReq, "nonce=\"", '"');
      std::string _url = _extractParam(authReq, "uri=\"", '"');
      std::string _resp = _extractParam(authReq, "response=\"", '"');
      std::string _opaque = _extractParam(authReq, "opaque=\"", '"');

      if ((!_realm.length()) || (!_nonce.length()) || (!_url.length()) || (!_resp.length()) || (!_opaque.length())) {
        authReq = "";
        return false;
      }
      if ((_opaque != this->getSessionKey("opaque")) || (_nonce != this->getSessionKey("nonce")) || (_realm != this->getSessionKey("realm"))) {
        authReq = "";
        return false;
      }
      // parameters for the RFC 2617 newer Digest
      std::string _nc, _cnonce;
      if (authReq.find("qop=auth") != std::string::npos || authReq.find("qop=\"auth\"") != std::string::npos) {
        _nc = _extractParam(authReq, "nc=", ',');
        _cnonce = _extractParam(authReq, "cnonce=\"", '"');
      }

      std::string _H1 = md5str(std::string(username) + ':' + _realm + ':' + std::string(password));
      // ESP_LOGD(PH_TAG, "Hash of user:realm:pass=%s", _H1.c_str());

      std::string _H2 = "";
      if (_method == HTTP_GET) {
        _H2 = md5str(std::string("GET:") + _url);
      } else if (_method == HTTP_POST) {
        _H2 = md5str(std::string("POST:") + _url);
      } else if (_method == HTTP_PUT) {
        _H2 = md5str(std::string("PUT:") + _url);
      } else if (_method == HTTP_DELETE) {
        _H2 = md5str(std::string("DELETE:") + _url);
      } else {
        _H2 = md5str(std::string("GET:") + _url);
      }
      // ESP_LOGD(PH_TAG, "Hash of GET:uri=%s", _H2.c_str());

      std::string _responsecheck = "";
      if (authReq.find("qop=auth") != std::string::npos || authReq.find("qop=\"auth\"") != std::string::npos) {
        _responsecheck = md5str(_H1 + ':' + _nonce + ':' + _nc + ':' + _cnonce + ":auth:" + _H2);
      } else {
        _responsecheck = md5str(_H1 + ':' + _nonce + ':' + _H2);
      }

      // ESP_LOGD(PH_TAG, "The Proper response=%s", _responsecheck.c_str());
      if (_resp == _responsecheck) {
        authReq = "";
        return true;
      }
    }
    authReq = "";
  }
  return false;
}

const std::string PsychicRequest::_extractParam(const std::string& authReq, const std::string& param, const char delimit)
{
  size_t _begin = authReq.find(param);
  if (_begin == std::string::npos)
    return "";
  return authReq.substr(_begin + param.length(), authReq.find(delimit, _begin + param.length()) - (_begin + param.length()));
}

const std::string PsychicRequest::_getRandomHexString()
{
  char buffer[33]; // buffer to hold 32 Hex Digit + /0
  int i;
  for (i = 0; i < 4; i++) {
    sprintf(buffer + (i * 8), "%08lx", (unsigned long int)esp_random());
  }
  return std::string(buffer);
}

esp_err_t PsychicRequest::requestAuthentication(HTTPAuthMethod mode, const char* realm, const char* authFailMsg)
{
  // what is thy realm, sire?
  if (!strcmp(realm, ""))
    this->setSessionKey("realm", "Login Required");
  else
    this->setSessionKey("realm", realm);

  PsychicResponse response(this);
  std::string authStr;

  // what kind of auth?
  if (mode == BASIC_AUTH) {
    authStr = "Basic realm=\"" + this->getSessionKey("realm") + "\"";
    response.addHeader("WWW-Authenticate", authStr.c_str());
  } else {
    // only make new ones if we havent sent them yet
    if (this->getSessionKey("nonce").empty())
      this->setSessionKey("nonce", _getRandomHexString());
    if (this->getSessionKey("opaque").empty())
      this->setSessionKey("opaque", _getRandomHexString());

    authStr = "Digest realm=\"" + this->getSessionKey("realm") + "\", qop=\"auth\", nonce=\"" + this->getSessionKey("nonce") + "\", opaque=\"" + this->getSessionKey("opaque") + "\"";
    response.addHeader("WWW-Authenticate", authStr.c_str());
  }

  response.setCode(401);
  response.setContentType("text/html");
  response.setContent(authFailMsg);
  return response.send();
}

} // namespace PsychicHttp
