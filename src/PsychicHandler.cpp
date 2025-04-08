#include "PsychicHandler.h"

PsychicHandler::PsychicHandler() : _filter(NULL),
                                   _server(NULL),
                                   _username(""),
                                   _password(""),
                                   _method(DIGEST_AUTH),
                                   _realm(""),
                                   _authFailMsg(""),
                                   _subprotocol("")
{
}

PsychicHandler::~PsychicHandler()
{
  // actual PsychicClient deletion handled by PsychicServer
  // for (PsychicClient *client : _clients)
  //   delete(client);
  _clients.clear();
}

PsychicHandler *PsychicHandler::setFilter(PsychicRequestFilterFunction fn)
{
  _filter = fn;
  return this;
}

bool PsychicHandler::filter(PsychicRequest *request)
{
  return _filter == NULL || _filter(request);
}

void PsychicHandler::setSubprotocol(const std::string &subprotocol)
{
  this->_subprotocol = subprotocol;
}
const char *PsychicHandler::getSubprotocol() const
{
  return _subprotocol.c_str();
}

PsychicHandler *PsychicHandler::setAuthentication(const char *username, const char *password, HTTPAuthMethod method, const char *realm, const char *authFailMsg)
{
  _username = username;
  _password = password;
  _method = method;
  _realm = realm;
  _authFailMsg = authFailMsg;
  return this;
}

bool PsychicHandler::needsAuthentication(PsychicRequest *request)
{
  return (_username && _password && *_username && *_password) &&
         !request->authenticate(_username, _password);
}

esp_err_t PsychicHandler::authenticate(PsychicRequest *request)
{
  return request->requestAuthentication(_method, _realm, _authFailMsg);
}

PsychicClient *PsychicHandler::checkForNewClient(PsychicClient *client)
{
  PsychicClient *c = PsychicHandler::getClient(client);
  if (c == NULL)
  {
    c = client;
    addClient(c);
    c->isNew = true;
  }
  else
    c->isNew = false;

  return c;
}

void PsychicHandler::checkForClosedClient(PsychicClient *client)
{
  if (hasClient(client))
  {
    closeCallback(client);
    removeClient(client);
  }
}

void PsychicHandler::addClient(PsychicClient *client)
{
  _clients.push_back(client);
}

void PsychicHandler::removeClient(PsychicClient *client)
{
  _clients.remove(client);
}

PsychicClient *PsychicHandler::getClient(int socket)
{
  // make sure the server has it too.
  if (!_server->hasClient(socket))
    return NULL;

  // what about us?
  for (PsychicClient *client : _clients)
    if (client->socket() == socket)
      return client;

  // nothing found.
  return NULL;
}

PsychicClient *PsychicHandler::getClient(PsychicClient *client)
{
  return PsychicHandler::getClient(client->socket());
}

bool PsychicHandler::hasClient(PsychicClient *socket)
{
  return PsychicHandler::getClient(socket) != NULL;
}

const std::list<PsychicClient *> &PsychicHandler::getClientList()
{
  return _clients;
}