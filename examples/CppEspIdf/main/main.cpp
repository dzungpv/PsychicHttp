
#include <cstring>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_littlefs.h"
#include "mdns.h"
#include <ArduinoJson.h>            // header-only, if you wish to keep using it
#include <PsychicHttp.h>
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE //set this to y in menuconfig to enable SSL
#include <PsychicHttpsServer.h>
#endif
#include "lwip/ip_addr.h"  // for IPSTR/IP2STR
#include "_secret.h"                // #define WIFI_SSID, WIFI_PASS here

#ifndef WIFI_SSID
  #error "You must define WIFI_SSID and WIFI_PASS in secret.h"
#endif

static const char *TAG = "app";
static const char *ssid       = WIFI_SSID;
static const char *password   = WIFI_PASSWORD;
static const char *softap_ssid     = "PsychicHttp";
static const char *softap_password = "";

//credentials for the /auth-basic and /auth-digest examples
const char *app_user = "admin";
const char *app_pass = "admin";
const char *app_name = "Your App";

static const char *local_hostname  = "psychic";

AuthenticationMiddleware basicAuth;
AuthenticationMiddleware digestAuth;
CorsMiddleware corsMiddleware;
LoggingMiddleware loggingMiddleware;

#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
bool app_enable_ssl = true;
std::string server_cert;
std::string server_key;
#endif

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t eb, int32_t id, void* data);
static void init_wifi_ap_sta();
static void mount_littlefs();

#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
PsychicHttpsServer server;
#else
PsychicHttpServer server;
#endif
PsychicWebSocketHandler websocketHandler;
PsychicEventSource eventSource;

extern "C" void app_main()
{
    // 1. NVS & TCP/IP stack
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Wi-Fi in AP+STA mode
    init_wifi_ap_sta();

    // 3. LittleFS mount
    mount_littlefs();

    // 4. mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(local_hostname));
    mdns_instance_name_set("PsychicHttp");

    // 6. Wait for WiFi STA connection before starting server
    ESP_LOGI(TAG, "Waiting for WiFi STA connection...");

    // Wait for STA to connect and get IP
    bool sta_connected = false;
    while (!sta_connected) {
      wifi_ap_record_t ap_info;
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        esp_netif_ip_info_t ip_info;
        esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
          sta_connected = true;
          ESP_LOGI(TAG, "WiFi STA connected! IP: " IPSTR, IP2STR(&ip_info.ip));
        }
      }
      if (!sta_connected) {
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second
      }
    }

    // 5. Load SSL cert/key if enabled
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
    if (app_enable_ssl) {
        // Cert
        FILE *fcrt = fopen("/littlefs/server.crt", "rb");
        if (!fcrt) {
            ESP_LOGE(TAG, "server.crt not found, disabling SSL");
            app_enable_ssl = false;
        } else {
            fseek(fcrt, 0, SEEK_END);
            size_t cert_size = ftell(fcrt);
            rewind(fcrt);

            std::string cert;
            cert.resize(cert_size);
            size_t read_bytes = fread(&cert[0], 1, cert_size, fcrt);
            fclose(fcrt);

            if (read_bytes != cert_size) {
                ESP_LOGE(TAG, "Failed read cert (%u/%u)", (unsigned)read_bytes, (unsigned)cert_size);
                app_enable_ssl = false;
            } else {
                server_cert = std::move(cert);
            }
        }

        // Key
        FILE *fkey = fopen("/littlefs/server.key", "rb");
        if (!fkey) {
            ESP_LOGE(TAG, "server.key not found, disabling SSL");
            app_enable_ssl = false;
        } else {
            fseek(fkey, 0, SEEK_END);
            size_t key_size = ftell(fkey);
            rewind(fkey);

            std::string key;
            key.resize(key_size);
            size_t read_bytes = fread(&key[0], 1, key_size, fkey);
            fclose(fkey);

            if (read_bytes != key_size) {
                ESP_LOGE(TAG, "Failed read key (%u/%u)", (unsigned)read_bytes, (unsigned)key_size);
                app_enable_ssl = false;
            } else {
                server_key = std::move(key);
            }
        }
    }
#endif
    // 7. Configure and start HTTP(S) server
    server.config.max_uri_handlers = 20;
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
    server.ssl_config.httpd.max_uri_handlers = 20;
    if (app_enable_ssl) {
      server.setCertificate(server_cert.c_str(), server_key.c_str());
      // this creates a 2nd server listening on port 80 and redirects all requests HTTPS
      PsychicHttpServer* redirectServer = new PsychicHttpServer();
      redirectServer->config.ctrl_port = 20424; // just a random port different from the default one
      redirectServer->onNotFound([](PsychicRequest* request, PsychicResponse* response) {
          std::string url = "https://" + request->host() + request->url();
          return response->redirect(url.c_str()); });
    }
#endif

    DefaultHeaders::Instance().addHeader("Server", "PsychicHttp");

    // loggingMiddleware.setOutput(Serial);

    basicAuth.setUsername(app_user);
    basicAuth.setPassword(app_pass);
    basicAuth.setRealm(app_name);
    basicAuth.setAuthMethod(HTTPAuthMethod::BASIC_AUTH);
    basicAuth.setAuthFailureMessage("You must log in.");

    digestAuth.setUsername(app_user);
    digestAuth.setPassword(app_pass);
    digestAuth.setRealm(app_name);
    digestAuth.setAuthMethod(HTTPAuthMethod::DIGEST_AUTH);
    digestAuth.setAuthFailureMessage("You must log in.");

    // corsMiddleware.setAllowCredentials(true);
    // corsMiddleware.setOrigin("http://www.example.com,https://www.example.com,http://api.example.com,https://api.example.com");
    // corsMiddleware.setHeaders("Origin,X-Requested-With,Content-Type,Accept,Content-Type,Authorization,X-Access-Token");

    // server.addMiddleware(&loggingMiddleware);
    // this will send CORS headers on every HTTP_OPTIONS request that contains the Origin: header
    server.addMiddleware(&corsMiddleware);

    // rewrites!
    server.rewrite("/rewrite", "/api?foo=rewrite");

    // serve static files from littlefs/www on / only to clients on same wifi network
    // this is where our /index.html file lives
    //  curl -i http://psychic.local/
    server.serveStatic("/", "/littlefs/www/")
      ->setCacheControl("max-age=60")
      ->addFilter(ON_STA_FILTER);

    // serve static files from littlefs/www-ap on / only to clients on SoftAP
    // this is where our /index.html file lives
    server.serveStatic("/", "/littlefs/www-ap/")->addFilter(ON_AP_FILTER);

    // serve static files from littlefs/img on /img
    // it's more efficient to serve everything from a single www directory, but this is also possible.
    //  curl -i http://psychic.local/img/request_flow.png
    server.serveStatic("/img", "/littlefs/img/");

#ifdef PSY_ENABLE_SDCARD
    // if we detect an SD card, serve all files from sd:/ on http://psychic.local/sd
    if (setupSDCard())
      server.serveStatic("/sd", SD, "/");
#endif

    // you can also serve single files
    //  curl -i http://psychic.local/myfile.txt
    server.serveStatic("/myfile.txt", "/littlefs/custom.txt");

    // example callback everytime a connection is opened
    server.onOpen([](PsychicClient* client) {
        ip4_addr_t ip = client->remoteIP();
        // format it into a C-string
        const char* ip_string = ip4addr_ntoa(&ip);
        ESP_LOGI(TAG, "[http] connection #%u connected from %s\n", client->socket(), ip_string);
    });

    // example callback everytime a connection is closed
    server.onClose([](PsychicClient* client) {
         ESP_LOGI(TAG, "[http] connection #%u closed\n", client->socket()); 
    });

    // api - json message passed in as post body
    //  curl -i -X POST -H "Content-Type: application/json" -d '{"foo":"bar"}' http://psychic.local/api
    server.on("/api", HTTP_POST, [](PsychicRequest* request, PsychicResponse* resp, JsonVariant& json) {
      JsonObject input = json.as<JsonObject>();

      JsonDocument output;
      output["msg"] = "status";
      output["status"] = "success";
      output["millis"] = (unsigned long)(esp_timer_get_time() / 1000);
      output["method"] = request->methodStr().c_str();
      if (input.containsKey("foo")) {
        std::string foo = input["foo"];
        output["foo"] = foo;
      }
      std::string serialized;
      serializeJson(output, serialized);
      return resp->send(200, "application/json", serialized.c_str());

      // Below code crash crash endless loop
#if 0
      JsonObject input = json.as<JsonObject>();
      // create our response json
      PsychicJsonResponse response(resp);
      JsonObject output = response.getRoot();

      output["msg"] = "status";
      output["status"] = "success";
      output["millis"] = (unsigned long)(esp_timer_get_time() / 1000);
      output["method"] = request->methodStr();

      // work with some params
      if (input.containsKey("foo")) {
        std::string foo = input["foo"];
        output["foo"] = foo;
      }

      return response.send();
#endif
    });

    // ip - get info about the client
    //  curl -i http://psychic.local/ip
    server.on("/ip", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {        // grab the raw ip4_addr_t
        ip4_addr_t ip = request->client()->remoteIP();
        // format it into a C-string
        const char *ip_string = ip4addr_ntoa(&ip);
        // build your response
        std::string out = std::string("Your IP is: ") + ip_string;
        return response->send(out.c_str());
    });
	
	// Simple endpoint print your IP using PsychicStreamResponse
    /*
    server.on("/stream", HTTP_GET, [](PsychicRequest *request, PsychicResponse* resp) {
        // grab the raw ip4_addr_t
        ip4_addr_t ip = request->remoteIP();
        // format it into a C-string
        const char *ip_string = ip4addr_ntoa(&ip);
        // build your response
        std::string out = std::string("Your IP is: ") + ip_string;
        // send to browser
        resp->beginSend();
        resp->print(out);
        return resp->endSend();
    });
    */

    // client connect/disconnect to a url
    //  curl -i http://psychic.local/handler
    PsychicWebHandler* connectionHandler = new PsychicWebHandler();
    connectionHandler->onRequest([](PsychicRequest* request, PsychicResponse* response) { return response->send("OK"); });
    connectionHandler->onOpen([](PsychicClient* client) {
        ip4_addr_t ip = client->remoteIP();
        // format it into a C-string
        const char *ip_string = ip4addr_ntoa(&ip);
        ESP_LOGI(TAG, "[handler] connection #%u connected from %s\n", client->socket(), ip_string);
    });
    connectionHandler->onClose([](PsychicClient* client) {
        ESP_LOGI(TAG, "[handler] connection #%u closed\n", client->socket());
    });

    // add it to our server
    server.on("/handler", connectionHandler);

    // api - parameters passed in via query eg. /api?foo=bar
    //  curl -i 'http://psychic.local/api?foo=bar'
    server.on("/api", HTTP_GET, [](PsychicRequest* request, PsychicResponse* resp) {
      JsonDocument doc;
      doc["msg"] = "status";
      doc["status"] = "success";
      doc["millis"] = (unsigned long)(esp_timer_get_time() / 1000);
      doc["method"] = request->methodStr().c_str();
      if (request->hasParam("foo")) {
          std::string foo = request->getParam("foo")->value();
          doc["foo"] = foo.c_str();
      }
      std::string output;
      serializeJson(doc, output);
      return resp->send(200, "application/json", output.c_str());

      // Below code crash crash endless loop
#if 0
      // create our response json
      PsychicJsonResponse response = PsychicJsonResponse(resp);
      JsonObject output = response.getRoot();

      output["msg"] = "status";
      output["status"] = "success";
      output["millis"] = (unsigned long)(esp_timer_get_time() / 1000);
      output["method"] = request->methodStr();

      // work with some params
      if (request->hasParam("foo")) {
        std::string foo = request->getParam("foo")->value();
        output["foo"] = foo;
      }

      return response.send();
#endif
    });

    // curl -i -X GET 'http://psychic.local/any'
    // curl -i -X POST 'http://psychic.local/any'
    server.on("/any", HTTP_ANY, [](PsychicRequest* request, PsychicResponse* resp) {
      JsonDocument output;
      output["msg"] = "status";
      output["status"] = "success";
      output["millis"] = (unsigned long)(esp_timer_get_time() / 1000);
      output["method"] = request->methodStr().c_str();
      // Serialize the output to a string
      std::string serialized;
      serializeJson(output, serialized);
      
      return resp->send(200, "application/json", serialized.c_str());
#if 0
      // create our response json
      PsychicJsonResponse response = PsychicJsonResponse(resp);
      JsonObject output = response.getRoot();

      output["msg"] = "status";
      output["status"] = "success";
      output["millis"] = (unsigned long)(esp_timer_get_time() / 1000);
      output["method"] = request->methodStr();

      return response.send();
#endif
    });

    // curl -i 'http://psychic.local/simple'
    server.on("/simple", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
            return response->send("Simple");
          })
      ->setURIMatchFunction(MATCH_SIMPLE);

#ifdef PSY_ENABLE_REGEX
    // curl -i 'http://psychic.local/regex/23'
    // curl -i 'http://psychic.local/regex/4223'
    server.on("^/regex/([\\d]+)/?$", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
            // look up our regex matches
            std::smatch matches;
            if (request->getRegexMatches(matches)) {
              std::string output;
              output += "Matches: " + String(matches.size()) + "<br/>\n";
              output += "Matched URI: " + String(matches.str(0).c_str()) + "<br/>\n";
              output += "Match 1: " + String(matches.str(1).c_str()) + "<br/>\n";

              return response->send(output.c_str());
            } else
              return response->send("No regex match.");
          })
      ->setURIMatchFunction(MATCH_REGEX);
#endif

    // JsonResponse example
    //  curl -i http://psychic.local/json
    server.on("/json", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {

      JsonDocument output;
      char key[16];
      char value[32];
      for (int i = 0; i < 100; i++) {
        sprintf(key, "key%d", i);
        sprintf(value, "value is %d", i);
        output[key] = value;
      }
      std::string serialized;
      serializeJson(output, serialized);
      return response->send(200, "application/json", serialized.c_str());

      // Below code crash crash endless loop
#if 0
      PsychicJsonResponse jsonResponse = PsychicJsonResponse(response);

      char key[16];
      char value[32];
      JsonObject root = jsonResponse.getRoot();
      for (int i = 0; i < 100; i++) {
        sprintf(key, "key%d", i);
        sprintf(value, "value is %d", i);
        root[key] = value;
      }

      return jsonResponse.send();
#endif
    });

    // how to redirect a request
    //  curl -i  http://psychic.local/redirect
    server.on("/redirect", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
         return response->redirect("/alien.png"); 
    });

    // how to do basic auth
    //  curl -i --user admin:admin http://psychic.local/auth-basic
    server.on("/auth-basic", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
            return response->send("Auth Basic Success!");
          })
      ->addMiddleware(&basicAuth);

    // how to do digest auth
    //  curl -i --user admin:admin http://psychic.local/auth-digest
    server.on("/auth-digest", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
            return response->send("Auth Digest Success!");
          })
      ->addMiddleware(&digestAuth);

    // example of getting / setting cookies
    //  curl -i -b cookie.txt -c cookie.txt http://psychic.local/cookies
    server.on("/cookies", HTTP_GET, [](PsychicRequest* request, PsychicResponse* response) {
      int counter = 0;
      char cookie[14];
      size_t size = sizeof(cookie);
      if (request->getCookie("counter", cookie, &size) == ESP_OK) {
        // value is null-terminated.
        counter = std::stoi(cookie);
        counter++;
      }
      sprintf(cookie, "%d", counter);

      response->setCookie("counter", cookie);
      response->setContent(cookie);
      return response->send();
    });

    // example of getting POST variables
    //  curl -i -d "param1=value1&param2=value2" -X POST http://psychic.local/post
    //  curl -F "param1=value1" -F "param2=value2" -X POST http://psychic.local/post
    server.on("/post", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) {
      std::string output;
      output += "Param 1: " + request->getParam("param1")->value() + "<br/>\n";
      output += "Param 2: " + request->getParam("param2")->value() + "<br/>\n";

      return response->send(output.c_str());
    });

    // you can set up a custom 404 handler.
    //  curl -i http://psychic.local/404
    server.onNotFound([](PsychicRequest* request, PsychicResponse* response) {
         return response->send(404, "text/html", "Custom 404 Handler"); 
    });

    // handle a very basic upload as post body
    PsychicUploadHandler* uploadHandler = new PsychicUploadHandler();
    uploadHandler->onUpload([](PsychicRequest* request, const std::string& filename, uint64_t index, uint8_t* data, size_t len, bool last) {
      // build a POSIX path under mounted littlefs
      char path[128];
      snprintf(path, sizeof(path), "/littlefs/www/%s", filename.c_str());

      // log progress
      ESP_LOGI(TAG, "Writing %u/%u bytes to: %s", (unsigned)(index + len), (unsigned)request->contentLength(), path);
      if (last) {
        ESP_LOGI(TAG, "%s finished. Total bytes: %llu", path, (unsigned long long)(index + len));
      }

      // open for write ("wb") on first chunk, append ("ab") thereafter
      const char* mode = (index == 0) ? "wb" : "ab";
      FILE* file = fopen(path, mode);
      if (!file) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_FAIL;
      }

      size_t written = fwrite(data, 1, len, file);
      fclose(file);

      if (written != len) {
        ESP_LOGE(TAG, "Write failed (%u/%u)", (unsigned)written, (unsigned)len);
        return ESP_FAIL;
      }
      return ESP_OK;
    });

    // gets called after upload has been handled
    uploadHandler->onRequest([](PsychicRequest* request, PsychicResponse* response) {
      std::string url = "/" + request->getFilename();
      std::string output = "<a href=\"" + url + "\">" + url + "</a>";

      return response->send(output.c_str());
    });

    // wildcard basic file upload - POST to /upload/filename.ext
    //  use http://psychic.local/ to test
    server.on("/upload/*", HTTP_POST, uploadHandler);

    // a little bit more complicated multipart form
    PsychicUploadHandler* multipartHandler = new PsychicUploadHandler();
    multipartHandler->onUpload([](PsychicRequest* request, const std::string& filename, uint64_t index, uint8_t* data, size_t len, bool last) {
      // build a POSIX path under mounted littlefs
      char path[128];
      snprintf(path, sizeof(path), "/littlefs/www/%s", filename.c_str());

      ESP_LOGI(TAG, "Writing %u bytes to: %s", (unsigned)len, path);
      if (last) {
        ESP_LOGI(TAG, "%s finished. Total bytes: %llu", path, (unsigned long long)(index + len));
      }

      // open with "wb" on first chunk, "ab" on subsequent
      FILE* file = nullptr;
      if (index == 0) {
        file = fopen(path, "wb");
      } else {
        file = fopen(path, "ab");
      }
      if (!file) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_FAIL;
      }

      size_t written = fwrite(data, 1, len, file);
      fclose(file);

      if (written != len) {
        ESP_LOGE(TAG, "Write failed (%u/%u)", (unsigned)written, (unsigned)len);
        return ESP_FAIL;
      }
      return ESP_OK;
    });

    // gets called after upload has been handled
    multipartHandler->onRequest([](PsychicRequest* request, PsychicResponse* response) {
      std::string output;
      if (request->hasParam("file_upload")) {
        PsychicWebParameter* file = request->getParam("file_upload");

        std::string url = "/" + file->value();
        output += "<a href=\"" + url + "\">" + url + "</a><br/>\n";
        output += "Bytes: " + std::to_string(file->size()) + "<br/>\n";
      }

      if (request->hasParam("param1"))
        output += "Param 1: " + request->getParam("param1")->value() + "<br/>\n";
      if (request->hasParam("param2"))
        output += "Param 2: " + request->getParam("param2")->value() + "<br/>\n";

      return response->send(output.c_str());
    });

    // wildcard basic file upload - POST to /upload/filename.ext
    //  use http://psychic.local/ to test
    // just multipart data: curl -F "param1=multi" -F "param2=part" http://psychic.local/multipart
    server.on("/multipart", HTTP_POST, multipartHandler);

    // form only multipart handler
    // curl -F "param1=multi" -F "param2=part" http://psychic.local/multipart-data
    PsychicUploadHandler* multipartFormHandler = new PsychicUploadHandler();
    multipartFormHandler->onRequest([](PsychicRequest* request, PsychicResponse* response) {
      std::string output;
      if (request->hasParam("param1"))
        output += "Param 1: " + request->getParam("param1")->value() + "<br/>\n";
      if (request->hasParam("param2"))
        output += "Param 2: " + request->getParam("param2")->value() + "<br/>\n";

      return response->send(output.c_str());
    });
    server.on("/multipart-data", HTTP_POST, multipartFormHandler);

    // a websocket echo server
    //  npm install -g wscat
    // Plaintext: wscat -c ws://psychic.local/ws
    // SSL: wscat -n -c wss://psychic.local/ws
    websocketHandler.onOpen([](PsychicWebSocketClient* client) {
      ip4_addr_t ip = client->remoteIP();
      // format it into a C-string
      const char* ip_string = ip4addr_ntoa(&ip);
      ESP_LOGI(TAG, "[socket] connection #%u connected from %s\n", client->socket(), ip_string);
      client->sendMessage("Hello!");
    });
    websocketHandler.onFrame([](PsychicWebSocketRequest* request, httpd_ws_frame* frame) {
      // ESP_LOGI(TAG, "[socket] #%d sent: %s\n", request->client()->socket(), String((char*)frame->payload, frame->len).c_str());
      return request->reply(frame);
    });
    websocketHandler.onClose([](PsychicWebSocketClient* client) {
        ESP_LOGI(TAG, "[socket] connection #%u closed\n", client->socket());
    });
    server.on("/ws", &websocketHandler);

    // EventSource server
    //  curl -i -N http://psychic.local/events
    eventSource.onOpen([](PsychicEventSourceClient* client) {
      ip4_addr_t ip = client->remoteIP();
      // format it into a C-string
      const char* ip_string = ip4addr_ntoa(&ip);
      ESP_LOGI(TAG, "[eventsource] connection #%u connected from %s\n", client->socket(), ip_string);
      client->send("Hello user!", NULL, (unsigned long)(esp_timer_get_time() / 1000), 1000);
    });
    eventSource.onClose([](PsychicEventSourceClient* client) {
        ESP_LOGI(TAG, "[eventsource] connection #%u closed\n", client->socket());
    });
    server.on("/events", &eventSource);

    // example of using POST data inside the filter
    // works: curl -F "secret=password" http://psychic.local/post-filter
    // 404:   curl -F "foo=bar" http://psychic.local/post-filter
    server.on("/post-filter", HTTP_POST, [](PsychicRequest* request, PsychicResponse* response) {
            std::string output;
            output += "Secret: " + request->getParam("secret")->value() + "<br/>\n";

            return response->send(output.c_str());
          })
      ->addFilter([](PsychicRequest* request) {
        request->loadParams();
        return request->hasParam("secret");
      });

    server.begin();

    // Arduino like “loop” task to push millis to WS & ES
    xTaskCreate([](void*){
      uint32_t last = (unsigned long)(esp_timer_get_time() / 1000);
      char buf[32];
      for(;;) {
        uint32_t now = (unsigned long)(esp_timer_get_time() / 1000);
        if (now - last > 2000) {
            int len = snprintf(buf, sizeof(buf), "Millis: %lu\n", now);
            if (len > 0) {
                websocketHandler.sendAll(buf);
                snprintf(buf, sizeof(buf), "%lu", now);
                eventSource.send(buf, "millis", now, 0);
            }
            last = now;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }, "loop", 8*1024, nullptr, tskIDLE_PRIORITY, nullptr);
}

// ——————————————————————————————————————————
// Helper implementations
// ——————————————————————————————————————————

static void init_wifi_ap_sta()
{
    // create default netifs
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    // init Wi-Fi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr));

    // set mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // AP config
    wifi_config_t ap_cfg = {};
    std::strncpy((char*)ap_cfg.ap.ssid, softap_ssid, 32);
    ap_cfg.ap.ssid_len    = strlen(softap_ssid);
    ap_cfg.ap.authmode    = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    // STA config
    wifi_config_t st_cfg = {};
    std::strncpy((char*)st_cfg.sta.ssid, ssid, 32);
    std::strncpy((char*)st_cfg.sta.password, password, 64);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &st_cfg));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi AP+STA started (waiting for STA connection before server start)");
}

static void mount_littlefs()
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",         // mount location
        .partition_label = "littlefs",    // default partition label from partition table
        .format_if_mount_failed = false,  // format if mount fail
        .dont_mount = false               // false = register + mount
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LITTLEFS mount failed: %s", esp_err_to_name(err));
        return;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "LITTLEFS mounted: total=%u, used=%u", total, used);
}

static void wifi_event_handler(void* arg, esp_event_base_t eb, int32_t id, void* data)
{
    if (eb == WIFI_EVENT) {
        switch (id) {
          case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
          case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "STA disconnected, retrying...");
            esp_wifi_connect();
            break;
          default:
            break;
        }
    } else if (eb == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&ev->ip_info.ip));
    }
}