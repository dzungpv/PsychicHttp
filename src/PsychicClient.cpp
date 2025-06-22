#include "PsychicClient.h"
#include "PsychicHttpServer.h"
#include <lwip/sockets.h>
#include "esp_log.h"

PsychicClient::PsychicClient(httpd_handle_t server, int socket) :
  _server(server),
  _socket(socket),
  _friend(nullptr),
  isNew(false)
{}

PsychicClient::~PsychicClient() {
}

httpd_handle_t PsychicClient::server() {
  return _server;
}

int PsychicClient::socket() {
  return _socket;
}

// I'm not sure this is entirely safe to call.  I was having issues with race conditions when highly loaded using this.
esp_err_t PsychicClient::close()
{
  esp_err_t err = httpd_sess_trigger_close(_server, _socket);
  //PsychicHttpServer::closeCallback(_server, _socket); // call this immediately so the client is taken off the list.

  return err;
}

// Return type: ip4_addr_t from lwIP.
ip4_addr_t PsychicClient::localIP() {
  ip4_addr_t ip_addr;
  ip4_addr_set_zero(&ip_addr);  // Initialize to 0.0.0.0

  char ipstr[INET6_ADDRSTRLEN];
  struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
  socklen_t addr_size = sizeof(addr);

  if (getsockname(_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
      ESP_LOGE(PH_TAG, "Error getting client IP");
      return ip_addr;
  }

  // Convert the last 32 bits of the IPv6 address to an IPv4 string.
  // This is common when IPv4 is embedded in an IPv6-mapped address.
  if (!inet_ntop(AF_INET, &addr.sin6_addr.un.u32_addr[3], ipstr, sizeof(ipstr))) {
      ESP_LOGE(PH_TAG, "inet_ntop failed");
      return ip_addr;
  }

  // Convert the IPv4 string to an ip4_addr_t (in network byte order)
  ip_addr.addr = ipaddr_addr(ipstr);
  return ip_addr;
}

ip4_addr_t PsychicClient::remoteIP() {
  ip4_addr_t ip_addr;
  ip4_addr_set_zero(&ip_addr);  // Initialize to 0.0.0.0

  char ipstr[INET6_ADDRSTRLEN];
  struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
  socklen_t addr_size = sizeof(addr);

  if (getpeername(_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
      ESP_LOGE(PH_TAG, "Error getting remote IP");
      return ip_addr;
  }

  // Convert embedded IPv4 from IPv6 address.
  if (!inet_ntop(AF_INET, &addr.sin6_addr.un.u32_addr[3], ipstr, sizeof(ipstr))) {
      ESP_LOGE(PH_TAG, "inet_ntop failed");
      return ip_addr;
  }

  // Convert the string to an ip4_addr_t.
  ip_addr.addr = ipaddr_addr(ipstr);
  return ip_addr;
}