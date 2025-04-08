#include "PsychicClient.h"
#include "PsychicHttpServer.h"
#include <lwip/sockets.h>

PsychicClient::PsychicClient(httpd_handle_t server, int socket) :
  _server(server),
  _socket(socket),
  _friend(NULL),
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

// IPAddress PsychicClient::localIP()
// {
//   IPAddress address(0,0,0,0);

//   char ipstr[INET6_ADDRSTRLEN];
//   struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
//   socklen_t addr_size = sizeof(addr);

//   if (getsockname(_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
//     ESP_LOGE(PH_TAG, "Error getting client IP");
//     return address;
//   }

//   // Convert to IPv4 string
//   inet_ntop(AF_INET, &addr.sin6_addr.un.u32_addr[3], ipstr, sizeof(ipstr));
//   //ESP_LOGD(PH_TAG, "Client Local IP => %s", ipstr);
//   address.fromString(ipstr);

//   return address;
// }

ip4_addr_t PsychicClient::localIP()
{
    ip4_addr_t myIP;
    inet_aton("0.0.0.0", &myIP);  // Default to 0.0.0.0

    struct sockaddr_in6 addr;  // ESP-IDF uses IPv6 in esp_http_server
    socklen_t addr_size = sizeof(addr);

    if (getsockname(_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
        ESP_LOGE(PH_TAG, "Error getting client IP");
        return myIP;
    }

    // Extract IPv4-mapped address from sockaddr_in6
    if (IN6_IS_ADDR_V4MAPPED(&addr.sin6_addr)) {
        uint32_t ipv4_raw = addr.sin6_addr.un.u32_addr[3]; // Extract IPv4 part
        myIP.addr = ipv4_raw; // Store as ip4_addr_t
    } else {
        ESP_LOGE(PH_TAG, "Not an IPv4-mapped address");
    }

    ESP_LOGD(PH_TAG, "Client Local IP => %s", ip4addr_ntoa(&myIP));
    return myIP;
}

// IPAddress PsychicClient::remoteIP()
// {
//   IPAddress address(0,0,0,0);

//   char ipstr[INET6_ADDRSTRLEN];
//   struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
//   socklen_t addr_size = sizeof(addr);

//   if (getpeername(_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
//     ESP_LOGE(PH_TAG, "Error getting client IP");
//     return address;
//   }

//   // Convert to IPv4 string
//   inet_ntop(AF_INET, &addr.sin6_addr.un.u32_addr[3], ipstr, sizeof(ipstr));
//   //ESP_LOGD(PH_TAG, "Client Remote IP => %s", ipstr);
//   address.fromString(ipstr);

//   return address;
// }

ip4_addr_t PsychicClient::remoteIP()
{
    ip4_addr_t remoteIP;
    inet_aton("0.0.0.0", &remoteIP);  // Default to 0.0.0.0

    struct sockaddr_in6 addr;  // ESP-IDF uses IPv6 in esp_http_server
    socklen_t addr_size = sizeof(addr);

    if (getpeername(_socket, (struct sockaddr *)&addr, &addr_size) < 0) {
        ESP_LOGE(PH_TAG, "Error getting remote IP");
        return remoteIP;
    }

    // Extract IPv4-mapped address from sockaddr_in6
    if (IN6_IS_ADDR_V4MAPPED(&addr.sin6_addr)) {
        uint32_t ipv4_raw = addr.sin6_addr.un.u32_addr[3]; // Extract IPv4 part
        remoteIP.addr = ipv4_raw; // Store as ip4_addr_t
    } else {
        ESP_LOGE(PH_TAG, "Not an IPv4-mapped address");
    }

    ESP_LOGD(PH_TAG, "Client Remote IP => %s", ip4addr_ntoa(&remoteIP));
    return remoteIP;
}