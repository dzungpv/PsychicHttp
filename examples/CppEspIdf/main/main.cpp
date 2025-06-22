
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
#include "ArduinoJson.h"            // header-only, if you wish to keep using it
#include "PsychicHttp.h"
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE //set this to y in menuconfig to enable SSL
#include "PsychicHttpsServer.h"
#endif
#include "lwip/ip_addr.h"  // for IPSTR/IP2STR
#include "secret.h"                // #define WIFI_SSID, WIFI_PASS here

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

    // 5. Load SSL cert/key if enabled
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
    if (app_enable_ssl) {
        FILE *f = fopen("/littlefs/server.crt", "r");
        if (f) {
            char buf[4096];
            size_t r = fread(buf, 1, sizeof(buf) - 1, f);
            buf[r] = '\0';
            server_cert = buf;
            fclose(f);
        } else {
            ESP_LOGW(TAG, "server.crt not found, disabling SSL");
            app_enable_ssl = false;
        }
        f = fopen("/littlefs/server.key", "r");
        if (f) {
            char buf[4096];
            size_t r = fread(buf, 1, sizeof(buf) - 1, f);
            buf[r] = '\0';
            server_key = buf;
            fclose(f);
        } else {
            ESP_LOGW(TAG, "server.key not found, disabling SSL");
            app_enable_ssl = false;
        }
    }
#endif

    // 6. Configure and start HTTP(S) server
    server.config.max_uri_handlers = 20;
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
    server.ssl_config.httpd.max_uri_handlers = 20;
    if (app_enable_ssl) {
        server.listen(443, server_cert.c_str(), server_key.c_str());
        // redirector on 80 → HTTPS
        auto *redir = new PsychicHttpServer();
        redir->config.ctrl_port = 20424;
        redir->listen(80);
        redir->onNotFound([](PsychicRequest *req) {
            std::string url = "https://" + req->host() + req->url();
            return req->redirect(url.c_str());
        });
    } else {
        server.listen(80);
    }
#else
    server.listen(80);
#endif

    // serve static files from littlefs/www on / only to clients on same wifi network
    // this is where our /index.html file lives
    server.serveStatic("/", "/littlefs/www/")->setFilter(ON_STA_FILTER);

    // serve static files from littlefs/www-ap on / only to clients on SoftAP
    // this is where our /index.html file lives
    server.serveStatic("/", "/littlefs/www-ap/")->setFilter(ON_AP_FILTER);

    // serve static files from littlefs/img on /img
    // it's more efficient to serve everything from a single www directory, but this is also possible.
    server.serveStatic("/img", "/littlefs/img/");

    // you can also serve single files
    server.serveStatic("/myfile.txt", "/littlefs/custom.txt");

    // Connection callback everytime a connection is opened
    server.onOpen([](PsychicClient *client) {
        // grab the raw ip4_addr_t
        ip4_addr_t ip = client->localIP();
        // format it into a C-string
        const char *ip_string = ip4addr_ntoa(&ip);
        ESP_LOGI(TAG, "[http] connection %u connected from %s", client->socket(), ip_string);
    });

    // Connection callback everytime a connection is closed
    server.onClose([](PsychicClient *client) {
        // grab the raw ip4_addr_t
        ip4_addr_t ip = client->localIP();
        // format it into a C-string
        const char *ip_string = ip4addr_ntoa(&ip);
        ESP_LOGI(TAG, "[http] connection %u closed from %s", client->socket(), ip_string);
    });

    // api - json message passed in as post body
    server.on("/api", HTTP_POST, [](PsychicRequest *request) {
        // load our JSON request
        JsonDocument json;
        std::string body = request->body();
        DeserializationError err = deserializeJson(json, body);

        // create our response json
        JsonDocument output;
        output["msg"] = "status";

        // did it parse?
        if (err) {
            output["status"] = "failure";
            output["error"] = err.c_str();
        } else {
            output["status"] = "success";
            output["millis"] = (unsigned long)(esp_timer_get_time() / 1000);

            // work with some params
            if (json.containsKey("foo")) {
                std::string foo = json["foo"];
                output["foo"] = foo;
            }
        }

        // serialize and return
        std::string jsonBuffer;
        serializeJson(output, jsonBuffer);
        return request->reply(200, "application/json", jsonBuffer.c_str());
    });

    // Simple endpoint print your IP
    server.on("/ip", HTTP_GET, [](PsychicRequest *req) {
        // grab the raw ip4_addr_t
        ip4_addr_t ip = req->client()->remoteIP();
        // format it into a C-string
        const char *ip_string = ip4addr_ntoa(&ip);
        // build your response
        std::string out = std::string("Your IP is: ") + ip_string;
        return req->reply(out.c_str());
    });

    // api - parameters passed in via query eg. /api/endpoint?foo=bar
    server.on("/api", HTTP_GET, [](PsychicRequest *request) {
        // create a response object
        JsonDocument output;
        output["msg"] = "status";
        output["status"] = "success";
        output["millis"] = (unsigned long)(esp_timer_get_time() / 1000);

        // work with some params
        if (request->hasParam("foo")) {
            std::string foo = request->getParam("foo")->name();
            output["foo"] = foo;
        }

        // serialize and return
        std::string jsonBuffer;
        serializeJson(output, jsonBuffer);
        return request->reply(200, "application/json", jsonBuffer.c_str());
    });

    // how to redirect a request
    server.on("/redirect", HTTP_GET, [](PsychicRequest *request) {
        return request->redirect("/alien.png");
    });

    // how to do basic auth
    server.on("/auth-basic", HTTP_GET, [](PsychicRequest *request) {
        if (!request->authenticate(app_user, app_pass))
            return request->requestAuthentication(BASIC_AUTH, app_name, "You must log in.");
        return request->reply("Auth Basic Success!");
    });

    // how to do digest auth
    server.on("/auth-digest", HTTP_GET, [](PsychicRequest *request) {
        if (!request->authenticate(app_user, app_pass))
            return request->requestAuthentication(DIGEST_AUTH, app_name, "You must log in.");
        return request->reply("Auth Digest Success!");
    });

    // example of getting / setting cookies
    server.on("/cookies", HTTP_GET, [](PsychicRequest *request) {
        PsychicResponse response(request);

        int counter = 0;
        if (request->hasCookie("counter")) {
            counter = std::stoi(request->getCookie("counter").c_str());
            counter++;
        }

        char cookie[12];
        sprintf(cookie, "%i", counter);

        response.setCookie("counter", cookie);
        response.setContent(cookie);
        return response.send();
    });

    // example of getting POST variables
    server.on("/post", HTTP_POST, [](PsychicRequest *request) {
        std::string output;
        output += "Param 1: " + request->getParam("param1")->value() + "<br/>\n";
        output += "Param 2: " + request->getParam("param2")->value() + "<br/>\n";

        return request->reply(output.c_str());
    });

    // you can set up a custom 404 handler.
    server.onNotFound([](PsychicRequest *request) {
        return request->reply(404, "text/html", "Custom 404 Handler");
    });

    // handle a very basic upload as post body
    PsychicUploadHandler *uploadHandler = new PsychicUploadHandler();
    uploadHandler->onUpload([](PsychicRequest *request, const std::string &filename, uint64_t index, uint8_t *data, size_t len, bool last) {
        // build a POSIX path under mounted littlefs
        char path[128];
        snprintf(path, sizeof(path), "/littlefs/www/%s", filename.c_str());

        // log progress
        ESP_LOGI(TAG, "Writing %u/%u bytes to: %s", (unsigned)(index + len), (unsigned)request->contentLength(), path);
        if (last) {
            ESP_LOGI(TAG, "%s finished. Total bytes: %llu", path, (unsigned long long)(index + len));
        }

        // open for write ("wb") on first chunk, append ("ab") thereafter
        const char *mode = (index == 0) ? "wb" : "ab";
        FILE *file = fopen(path, mode);
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
    uploadHandler->onRequest([](PsychicRequest *request) {
        std::string url = "/" + std::string(request->getFilename());
        std::string output = "<a href=\"" + url + "\">" + url + "</a>";

        return request->reply(output.c_str());
    });

    // wildcard basic file upload - POST to /upload/filename.ext
    server.on("/upload/*", HTTP_POST, uploadHandler);

    // a little bit more complicated multipart form
    PsychicUploadHandler *multipartHandler = new PsychicUploadHandler();
    multipartHandler->onUpload([](PsychicRequest *request, const std::string &filename, uint64_t index, uint8_t *data, size_t len, bool last) {
        // build a POSIX path under mounted littlefs
        char path[128];
        snprintf(path, sizeof(path), "/littlefs/www/%s", filename.c_str());

        ESP_LOGI(TAG, "Writing %u bytes to: %s", (unsigned)len, path);
        if (last) {
            ESP_LOGI(TAG, "%s finished. Total bytes: %llu", path, (unsigned long long)(index + len));
        }

        // open with "wb" on first chunk, "ab" on subsequent
        FILE *file = nullptr;
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
    multipartHandler->onRequest([](PsychicRequest *request) {
        PsychicWebParameter *file = request->getParam("file_upload");

        std::string url = "/" + file->value();
        std::string output;

        output += "<a href=\"" + url + "\">" + url + "</a><br/>\n";
        output += "Bytes: " + std::to_string(file->size()) + "<br/>\n";
        output += "Param 1: " + request->getParam("param1")->value() + "<br/>\n";
        output += "Param 2: " + request->getParam("param2")->value() + "<br/>\n";

        return request->reply(output.c_str());
    });

    // wildcard basic file upload - POST to /upload/filename.ext
    server.on("/multipart", HTTP_POST, multipartHandler);

    // WebSocket echo server
    websocketHandler.onOpen([](PsychicWebSocketClient *client) {
        ESP_LOGI(TAG, "[ws] #%u open", client->socket());
        client->sendMessage("Hello!");
    });
    websocketHandler.onFrame([](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
        ESP_LOGI(TAG, "[ws] #%u >> %.*s", request->client()->socket(), frame->len, (char *)frame->payload);
        return request->reply(frame);
    });
    websocketHandler.onClose([](PsychicWebSocketClient *client) {
        ESP_LOGI(TAG, "[ws] #%u closed", client->socket());
    });
    server.on("/ws", &websocketHandler);
    
    // EventSource server
    eventSource.onOpen([](PsychicEventSourceClient *client) {
        ESP_LOGI(TAG, "[es] #%u open", client->socket());
        client->send("Hello user!", nullptr, (unsigned long)(esp_timer_get_time() / 1000), 1000);
    });
    eventSource.onClose([](PsychicEventSourceClient *client) {
        ESP_LOGI(TAG, "[es] #%u closed", client->socket());
    });
    server.on("/events", &eventSource);

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
    ESP_LOGI(TAG, "WiFi AP+STA started");
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