/*
  PsychicHTTP Server Example

  This example code is in the Public Domain (or CC0 licensed, at your option.)

  Unless required by applicable law or agreed to in writing, this
  software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
  CONDITIONS OF ANY KIND, either express or implied.
*/

/**********************************************************************************************
* Note: this demo relies on the following libraries (Install via Library Manager)
* ArduinoJson UrlEncode
**********************************************************************************************/

/**********************************************************************************************
* Note: this demo relies on various files to be uploaded on the LittleFS partition
* Follow instructions here: https://randomnerdtutorials.com/esp32-littlefs-arduino-ide/
**********************************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include "esp_littlefs.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include "secret.h"
#include <PsychicHttp.h>
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE //set this to y in menuconfig to enable SSL
#include <PsychicHttpsServer.h> 
#endif

#ifndef WIFI_SSID
  #error "You need to enter your wifi credentials. Rename secret.h to _secret.h and enter your credentials there."
#endif

static const char *TAG = "app";

const char *littlefs_patch = "/littlefs";
const char *littlefs_lable = "littlefs";

//Enter your WIFI credentials in secret.h
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;

// Set your SoftAP credentials
const char *softap_ssid = "PsychicHttp";
const char *softap_password = "";
IPAddress softap_ip(10, 0, 0, 1);  

//credentials for the /auth-basic and /auth-digest examples
const char *app_user = "admin";
const char *app_pass = "admin";
const char *app_name = "Your App";

//hostname for mdns (psychic.local)
const char *local_hostname = "psychic";

//#define CONFIG_ESP_HTTPS_SERVER_ENABLE to enable ssl
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
  bool app_enable_ssl = true;
  std::string server_cert;
  std::string server_key;
#endif

//our main server object
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
  PsychicHttpsServer server;
#else
  PsychicHttpServer server;
#endif
PsychicWebSocketHandler websocketHandler;
PsychicEventSource eventSource;

bool mount_littlefs()
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = littlefs_patch,         // mount location
        .partition_label = littlefs_lable,    // default partition label from partition table
        .format_if_mount_failed = false,  // format if mount fail
        .dont_mount = false               // false = register + mount
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LITTLEFS mount failed: %s", esp_err_to_name(err));
        return false;
    }

    size_t total = 0, used = 0;
    err = esp_littlefs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "LITTLEFS mounted: total=%u, used=%u", total, used);
    return err == ESP_OK;
}

bool connectToWifi()
{
  //dual client and AP mode
  WiFi.mode(WIFI_AP_STA);

  // Configure SoftAP
  WiFi.softAPConfig(softap_ip, softap_ip, IPAddress(255, 255, 255, 0)); // subnet FF FF FF 00
  WiFi.softAP(softap_ssid, softap_password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("SoftAP IP Address: ");
  Serial.println(myIP);

  Serial.println();
  Serial.print("[WiFi] Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  // Auto reconnect is set true as default
  // To set auto connect off, use the following function
  // WiFi.setAutoReconnect(false);

  // Will try for about 10 seconds (20x 500ms)
  int tryDelay = 500;
  int numberOfTries = 20;

  // Wait for the WiFi event
  while (true)
  {
    switch (WiFi.status())
    {
      case WL_NO_SSID_AVAIL:
        Serial.println("[WiFi] SSID not found");
        break;
      case WL_CONNECT_FAILED:
        Serial.print("[WiFi] Failed - WiFi not connected! Reason: ");
        return false;
        break;
      case WL_CONNECTION_LOST:
        Serial.println("[WiFi] Connection was lost");
        break;
      case WL_SCAN_COMPLETED:
        Serial.println("[WiFi] Scan is completed");
        break;
      case WL_DISCONNECTED:
        Serial.println("[WiFi] WiFi is disconnected");
        break;
      case WL_CONNECTED:
        Serial.println("[WiFi] WiFi is connected!");
        Serial.print("[WiFi] IP address: ");
        Serial.println(WiFi.localIP());
        return true;
        break;
      default:
        Serial.print("[WiFi] WiFi Status: ");
        Serial.println(WiFi.status());
        break;
    }
    delay(tryDelay);

    if (numberOfTries <= 0)
    {
      Serial.print("[WiFi] Failed to connect to WiFi!");
      // Use disconnect function to force stop trying to connect
      WiFi.disconnect();
      return false;
    }
    else
    {
      numberOfTries--;
    }
  }

  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(10);

  // We start by connecting to a WiFi network
  // To debug, please enable Core Debug Level to Verbose
  if (connectToWifi())
  {
    //set up our esp32 to listen on the local_hostname.local domain
    if (!MDNS.begin(local_hostname)) {
      Serial.println("Error starting mDNS");
      return;
    }
    MDNS.addService("http", "tcp", 80);

    if(!mount_littlefs())
    {
      Serial.println("LittleFS Mount Failed. Do Platform -> Build Filesystem Image and Platform -> Upload Filesystem Image from VSCode");
      return;
    }

    //look up our keys?
    #ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
        if (app_enable_ssl) {
            // Cert
            FILE *fcrt = fopen((std::string(littlefs_patch) + "/server.crt").c_str(), "rb");
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
            FILE *fkey = fopen((std::string(littlefs_patch) + "/server.key").c_str(), "rb");
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

    //setup server config stuff here
    server.config.max_uri_handlers = 20; //maximum number of uri handlers (.on() calls)

    #ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
      server.ssl_config.httpd.max_uri_handlers = 20; //maximum number of uri handlers (.on() calls)

      //do we want secure or not?
      if (app_enable_ssl)
      {
        server.listen(443, server_cert.c_str(), server_key.c_str());
        
        //this creates a 2nd server listening on port 80 and redirects all requests HTTPS
        PsychicHttpServer *redirectServer = new PsychicHttpServer();
        redirectServer->config.ctrl_port = 20424; // just a random port different from the default one
        redirectServer->listen(80);
        redirectServer->onNotFound([](PsychicRequest *request) {
          std::string url = "https://" + request->host() + std::string(request->url());
          return request->redirect(url.c_str());
        });
      }
      else
        server.listen(80);
    #else
      server.listen(80);
    #endif

    //serve static files from LittleFS/www on / only to clients on same wifi network
    //this is where our /index.html file lives
    server.serveStatic("/", (std::string(littlefs_patch) + "/www/").c_str())->setFilter(ON_STA_FILTER);

    //serve static files from LittleFS/www-ap on / only to clients on SoftAP
    //this is where our /index.html file lives
    server.serveStatic("/", (std::string(littlefs_patch) + "/www-ap/").c_str())->setFilter(ON_AP_FILTER);

    //serve static files from LittleFS/img on /img
    //it's more efficient to serve everything from a single www directory, but this is also possible.
    server.serveStatic("/img", (std::string(littlefs_patch) + "/img/").c_str());

    //you can also serve single files
    server.serveStatic("/myfile.txt", (std::string(littlefs_patch) + "/custom.txt").c_str());

    //example callback everytime a connection is opened
    server.onOpen([](PsychicClient *client) {
      Serial.printf("[http] connection #%u connected from %s\n", client->socket(), client->localIP().toString().c_str());
    });

    //example callback everytime a connection is closed
    server.onClose([](PsychicClient *client) {
      Serial.printf("[http] connection #%u closed from %s\n", client->socket(), client->localIP().toString().c_str());
    });

    //api - json message passed in as post body
    server.on("/api", HTTP_POST, [](PsychicRequest *request)
    {
      //load our JSON request
      JsonDocument json;
      std::string body = request->body();
      DeserializationError err = deserializeJson(json, body);

      //create our response json
      JsonDocument output;
      output["msg"] = "status";

      //did it parse?
      if (err)
      {
        output["status"] = "failure";
        output["error"] = err.c_str();
      }
      else
      {
        output["status"] = "success";
        output["millis"] = millis();

        //work with some params
        if (json.containsKey("foo"))
        {
          String foo = json["foo"];
          output["foo"] = foo;
        }
      }

      //serialize and return
      std::string jsonBuffer;
      serializeJson(output, jsonBuffer);
      return request->reply(200, "application/json", jsonBuffer.c_str());
    });

    //api - parameters passed in via query eg. /api/endpoint?foo=bar
    server.on("/ip", HTTP_GET, [](PsychicRequest *request)
    {
      String output = "Your IP is: " + request->client()->remoteIP().toString();
      return request->reply(output.c_str());
    });

    // Simple endpoint print your IP using PsychicStreamResponse
    server.on("/stream", HTTP_GET, [](PsychicRequest *request) {
        String output = "Your IP is: " + request->client()->remoteIP().toString();
        // send to browser
        PsychicStreamResponse response(request, "text/plain");
        response.beginSend();
        response.print(output.c_str());
        return response.endSend();
    });

    //api - parameters passed in via query eg. /api/endpoint?foo=bar
    server.on("/api", HTTP_GET, [](PsychicRequest *request)
    {
      //create a response object
      JsonDocument output;
      output["msg"] = "status";
      output["status"] = "success";
      output["millis"] = millis();

      //work with some params
      if (request->hasParam("foo"))
      {
        std::string foo = request->getParam("foo")->name();
        output["foo"] = foo;
      }

      //serialize and return
      String jsonBuffer;
      serializeJson(output, jsonBuffer);
      return request->reply(200, "application/json", jsonBuffer.c_str());
    });

    //how to redirect a request
    server.on("/redirect", HTTP_GET, [](PsychicRequest *request)
    {
      return request->redirect("/alien.png");
    });

    //how to do basic auth
    server.on("/auth-basic", HTTP_GET, [](PsychicRequest *request)
    {
      if (!request->authenticate(app_user, app_pass))
        return request->requestAuthentication(BASIC_AUTH, app_name, "You must log in.");
      return request->reply("Auth Basic Success!");
    });

    //how to do digest auth
    server.on("/auth-digest", HTTP_GET, [](PsychicRequest *request)
    {
      if (!request->authenticate(app_user, app_pass))
        return request->requestAuthentication(DIGEST_AUTH, app_name, "You must log in.");
      return request->reply("Auth Digest Success!");
    });

    //example of getting / setting cookies
    server.on("/cookies", HTTP_GET, [](PsychicRequest *request)
    {
      PsychicResponse response(request);

      int counter = 0;
      if (request->hasCookie("counter"))
      {
        counter = std::stoi(request->getCookie("counter").c_str());
        counter++;
      }

      char cookie[12];
      sprintf(cookie, "%i", counter);

      response.setCookie("counter", cookie);
      response.setContent(cookie);
      return response.send();
    });

    //example of getting POST variables
    server.on("/post", HTTP_POST, [](PsychicRequest *request)
    {
      std::string output;
      output += "Param 1: " + request->getParam("param1")->value() + "<br/>\n";
      output += "Param 2: " + request->getParam("param2")->value() + "<br/>\n";

      return request->reply(output.c_str());
    });

    //you can set up a custom 404 handler.
    server.onNotFound([](PsychicRequest *request)
    {
      return request->reply(404, "text/html", "Custom 404 Handler");
    });

    //handle a very basic upload as post body
    PsychicUploadHandler *uploadHandler = new PsychicUploadHandler();
    uploadHandler->onUpload([](PsychicRequest *request, const std::string& filename, uint64_t index, uint8_t *data, size_t len, bool last) {
        // build a POSIX path under mounted littlefs
        char path[128];
        snprintf(path, sizeof(path), (std::string(littlefs_patch) + "/www/%s").c_str(), filename.c_str());

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

    //gets called after upload has been handled
    uploadHandler->onRequest([](PsychicRequest *request)
    {
      std::string url = "/" + std::string(request->getFilename());
      std::string output = "<a href=\"" + url + "\">" + url + "</a>";

      return request->reply(output.c_str());
    });

    //wildcard basic file upload - POST to /upload/filename.ext
    server.on("/upload/*", HTTP_POST, uploadHandler);

    //a little bit more complicated multipart form
    PsychicUploadHandler *multipartHandler = new PsychicUploadHandler();
    multipartHandler->onUpload([](PsychicRequest *request, const std::string &filename, uint64_t index, uint8_t *data, size_t len, bool last) {
        // build a POSIX path under mounted littlefs
        char path[128];
        snprintf(path, sizeof(path), (std::string(littlefs_patch) + "/www/%s").c_str(), filename.c_str());

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

    //gets called after upload has been handled
    multipartHandler->onRequest([](PsychicRequest *request)
    {
      PsychicWebParameter *file = request->getParam("file_upload");

      std::string url = "/" + file->value();
      std::string output;

      output += "<a href=\"" + url + "\">" + url + "</a><br/>\n";
      output += "Bytes: " + std::to_string(file->size()) + "<br/>\n";
      output += "Param 1: " + request->getParam("param1")->value() + "<br/>\n";
      output += "Param 2: " + request->getParam("param2")->value() + "<br/>\n";
      
      return request->reply(output.c_str());
    });

    //wildcard basic file upload - POST to /upload/filename.ext
    server.on("/multipart", HTTP_POST, multipartHandler);

    //a websocket echo server
    websocketHandler.onOpen([](PsychicWebSocketClient *client) {
      Serial.printf("[socket] connection #%u connected from %s\n", client->socket(), client->localIP().toString().c_str());
      client->sendMessage("Hello!");
    });
    websocketHandler.onFrame([](PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
        Serial.printf("[socket] #%d sent: %s\n", request->client()->socket(), (char *)frame->payload);
        return request->reply(frame);
    });
    websocketHandler.onClose([](PsychicWebSocketClient *client) {
      Serial.printf("[socket] connection #%u closed from %s\n", client->socket(), client->localIP().toString().c_str());
    });
    server.on("/ws", &websocketHandler);

    //EventSource server
    eventSource.onOpen([](PsychicEventSourceClient *client) {
      Serial.printf("[eventsource] connection #%u connected from %s\n", client->socket(), client->localIP().toString().c_str());
      client->send("Hello user!", NULL, millis(), 1000);
    });
    eventSource.onClose([](PsychicEventSourceClient *client) {
      Serial.printf("[eventsource] connection #%u closed from %s\n", client->socket(), client->localIP().toString().c_str());
    });
    server.on("/events", &eventSource);
  }
}

unsigned long lastUpdate = 0;
char output[60];

void loop()
{
  if (millis() - lastUpdate > 2000)
  {
    sprintf(output, "Millis: %lu\n", millis());
    websocketHandler.sendAll(output);

    sprintf(output, "%lu", millis());
    eventSource.send(output, "millis", millis(), 0);

    lastUpdate = millis();
  }
  vTaskDelay(1 / portTICK_PERIOD_MS); // Feed WDT
}