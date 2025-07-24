/*
  Asynchronous WebServer library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "PsychicEventSource.h"
#include "esp_log.h"
#include <vector>

/*****************************************/
// PsychicEventSource - Handler
/*****************************************/

PsychicEventSource::PsychicEventSource() :
  PsychicHandler(),
  _onOpen(nullptr),
  _onClose(nullptr)
{}

PsychicEventSource::~PsychicEventSource() {
}

PsychicEventSourceClient * PsychicEventSource::getClient(int socket)
{
  PsychicClient *client = PsychicHandler::getClient(socket);

  if (client == nullptr)
    return nullptr;

  return (PsychicEventSourceClient *)client->_friend;
}

PsychicEventSourceClient * PsychicEventSource::getClient(PsychicClient *client) {
  return getClient(client->socket());
}

esp_err_t PsychicEventSource::handleRequest(PsychicRequest *request)
{
  //start our open ended HTTP response
  PsychicEventSourceResponse response(request);
  esp_err_t err = response.send();

  //lookup our client
  PsychicClient *client = checkForNewClient(request->client());
  if (client->isNew)
  {
    //did we get our last id?
    if(request->hasHeader("Last-Event-ID"))
    {
      PsychicEventSourceClient *buddy = getClient(client);
      buddy->_lastId = atoi(request->header("Last-Event-ID").c_str());
    }

    //let our handler know.
    openCallback(client);
  }

  return err;
}

PsychicEventSource * PsychicEventSource::onOpen(PsychicEventSourceClientCallback fn) {
  _onOpen = fn;
  return this;
}

PsychicEventSource * PsychicEventSource::onClose(PsychicEventSourceClientCallback fn) {
  _onClose = fn;
  return this;
}

void PsychicEventSource::addClient(PsychicClient *client) {
  client->_friend = new PsychicEventSourceClient(client);
  PsychicHandler::addClient(client);
}

void PsychicEventSource::removeClient(PsychicClient *client) {
  PsychicHandler::removeClient(client);
  auto buddy = static_cast<PsychicEventSourceClient *>(client->_friend);
  if (buddy) {
      delete buddy;
      client->_friend = nullptr;
  }
}

void PsychicEventSource::openCallback(PsychicClient *client) {
  PsychicEventSourceClient *buddy = getClient(client);
  if (buddy == nullptr)
  {
    return;
  }

  if (_onOpen != nullptr)
    _onOpen(buddy);
}

void PsychicEventSource::closeCallback(PsychicClient *client) {
  PsychicEventSourceClient *buddy = getClient(client);
  if (buddy == nullptr)
  {
    return;
  }

  if (_onClose != nullptr)
    _onClose(getClient(buddy));
}

/**
 * @brief Sends an event to all connected clients.
 * * This function now safely handles client disconnections.
 * It iterates through all clients, attempts to send the event, and collects
 * any clients for whom the send fails. It then properly removes these
 * disconnected clients after the loop, preventing a crash from using a stale handle.
 */
void PsychicEventSource::send(const char *message, const char *event, uint32_t id, uint32_t reconnect) 
{
    std::string ev = generateEventMessage(message, event, id, reconnect);
    std::vector<PsychicClient *> clientsToRemove;

    // First, iterate and send, collecting disconnected clients
    for (PsychicClient *c : _clients) {
        // Extra protection: check for nullptr and invalid _friend pointer
        if (!c || c->_friend == nullptr) {
            clientsToRemove.push_back(c);
            continue;
        }
        auto esc = static_cast<PsychicEventSourceClient *>(c->_friend);
        if (!esc) {
            clientsToRemove.push_back(c);
            continue;
        }
        // Send; If failed → make to delete
        if (!esc->sendEvent(ev.c_str())) {
            clientsToRemove.push_back(c);
        }
    }

    // Second, iterate through the disconnected clients and clean them up
    for (PsychicClient *c : clientsToRemove) {
        if (c) {
            closeCallback(c);  // Let the user application know
            removeClient(c);   // Remove from handler and clean up memory
        }
    }
}

/*****************************************/
// PsychicEventSourceClient
/*****************************************/

PsychicEventSourceClient::PsychicEventSourceClient(PsychicClient *client) :
  PsychicClient(client->server(), client->socket()),
  _lastId(0)
{
}

PsychicEventSourceClient::~PsychicEventSourceClient(){
}

/**
 * @brief Returns a boolean indicating send success.
 */
bool PsychicEventSourceClient::send(const char *message, const char *event, uint32_t id, uint32_t reconnect){
  std::string ev = generateEventMessage(message, event, id, reconnect);
  return sendEvent(ev.c_str());
}

/**
 * @brief Sends data and returns true on success, false on failure.
 * This prevents a crash by detecting if the underlying socket is closed.
 */
bool PsychicEventSourceClient::sendEvent(const char *event) {
  if (!event) return false;
  if (!this->server()) { 
      return false;
  }
  if (this->socket() < 0){
    return false;
  }
  int result;
  do {
    result = httpd_socket_send(this->server(), this->socket(), event, strlen(event), 0);
  } while (result == HTTPD_SOCK_ERR_TIMEOUT);

  if (result < 0) {
    ESP_LOGD(PH_TAG, "sendEvent to socket %d failed. Client likely disconnected.", this->socket());
    return false;
  }
  return true;
}

/*****************************************/
// PsychicEventSourceResponse
/*****************************************/

PsychicEventSourceResponse::PsychicEventSourceResponse(PsychicRequest *request) 
  : PsychicResponse(request)
{
}

esp_err_t PsychicEventSourceResponse::send() {
  std::string out;

  // Compose the basic HTTP response headers
  out += "HTTP/1.1 200 OK\r\n";
  out += "Content-Type: text/event-stream\r\n";
  out += "Cache-Control: no-cache\r\n";
  out += "Connection: keep-alive\r\n";

  // Append default headers
  for (const HTTPHeader& header : DefaultHeaders::Instance().getHeaders()) {
      out += header.field;
      out += ": ";
      out += header.value;
      out += "\r\n";
  }

  // Final empty line separates headers from body
  out += "\r\n";

  int result;
  do {
    result = httpd_send(_request->request(), out.c_str(), out.length());
  } while (result == HTTPD_SOCK_ERR_TIMEOUT);

  if (result < 0)
    ESP_LOGE(PH_TAG, "EventSource send failed with %s", esp_err_to_name(result));

  if (result > 0)
    return ESP_OK;
  else
    return ESP_ERR_HTTPD_RESP_SEND;
}

/*****************************************/
// Event Message Generator
/*****************************************/

std::string generateEventMessage(const char* message, const char* event, uint32_t id, uint32_t reconnect) {
  std::string ev;

  char line[64];

  if (reconnect) {
      snprintf(line, sizeof(line), "retry: %lu\r\n", reconnect);
      ev += line;
  }

  if (id) {
      snprintf(line, sizeof(line), "id: %lu\r\n", id);
      ev += line;
  }

  if (event != nullptr) {
      ev += "event: ";
      ev += event;
      ev += "\r\n";
  }

  if (message != nullptr) {
      ev += "data: ";
      ev += message;
      ev += "\r\n";
  }

  ev += "\r\n";
  return ev;
}
