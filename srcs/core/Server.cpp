#include "Server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <csignal>
#include <ctime>
#include "ResponseBuilder.hpp"

namespace
{
volatile std::sig_atomic_t stopping = 0;
void stopServer(int) { stopping = 1; }
class SignalPolicy
{
    typedef void (*Handler)(int);
    Handler _interrupt;
    Handler _terminate;
    Handler _pipe;
public:
    SignalPolicy() {
        stopping = 0;
        _interrupt = std::signal(SIGINT, stopServer);
        _terminate = std::signal(SIGTERM, stopServer);
        _pipe = std::signal(SIGPIPE, SIG_IGN);
        if (_interrupt == SIG_ERR || _terminate == SIG_ERR || _pipe == SIG_ERR) {
            restore();
            throw std::runtime_error("could not install signal policy");
        }
    }
    ~SignalPolicy() { restore(); }
    void restore() {
        if (_interrupt != SIG_ERR) std::signal(SIGINT, _interrupt);
        if (_terminate != SIG_ERR) std::signal(SIGTERM, _terminate);
        if (_pipe != SIG_ERR) std::signal(SIGPIPE, _pipe);
    }
};
class PendingSocket
{
public:
    explicit PendingSocket(int fd) : _fd(fd) {}
    ~PendingSocket() { if (_fd >= 0) close(_fd); }
    int get() const { return _fd; }
    void release() { _fd = -1; }
private:
    PendingSocket(const PendingSocket&);
    PendingSocket& operator=(const PendingSocket&);
    int _fd;
};

void setupError(const std::string& operation, int error)
{
    throw std::runtime_error(operation + ": " + std::strerror(error));
}

sockaddr_in addressFor(const ServerBlock& server)
{
    std::ostringstream port;
    port << server.getPort();
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    struct addrinfo* result = NULL;
    int error = getaddrinfo(server.getHost().c_str(), port.str().c_str(),
                            &hints, &result);
    if (error != 0)
        throw std::runtime_error(std::string("resolve listener: ") + gai_strerror(error));
    sockaddr_in address = *reinterpret_cast<sockaddr_in*>(result->ai_addr);
    freeaddrinfo(result);
    return address;
}
}

Server::Server(const Config& config) : _config(config) {}
Server::~Server() { closeAll(); }

void Server::initializeListeners()
{
    if (!_listeners.empty())
        throw std::logic_error("listeners are already initialized");
    const std::vector<ServerBlock>& servers = _config.getServers();
    if (servers.empty())
        throw std::runtime_error("cannot start without a loaded configuration");
    try
    {
        for (std::size_t i = 0; i < servers.size(); ++i)
        {
            const ServerBlock& settings = servers[i];
            std::ostringstream endpoint;
            endpoint << settings.getHost() << ':' << settings.getPort();
            sockaddr_in address = addressFor(settings);
            PendingSocket socketFd(socket(AF_INET, SOCK_STREAM, 0));
            if (socketFd.get() < 0)
                setupError("socket " + endpoint.str(), errno);
            int reuse = 1;
            if (setsockopt(socketFd.get(), SOL_SOCKET, SO_REUSEADDR,
                           &reuse, sizeof(reuse)) < 0)
                setupError("setsockopt " + endpoint.str(), errno);
            if (fcntl(socketFd.get(), F_SETFL, O_NONBLOCK) < 0)
                setupError("non-blocking listener " + endpoint.str(), errno);
            if (bind(socketFd.get(), reinterpret_cast<sockaddr*>(&address),
                     sizeof(address)) < 0)
                setupError("bind " + endpoint.str(), errno);
            if (listen(socketFd.get(), SOMAXCONN) < 0)
                setupError("listen " + endpoint.str(), errno);
            Listener listener;
            listener.fd = socketFd.get();
            listener.settings = &settings;
            _listeners.push_back(listener);
            socketFd.release();
        }
    }
    catch (...)
    {
        closeAll();
        throw;
    }
}

const std::vector<Server::Listener>& Server::getListeners() const
{
    return _listeners;
}

Client* Server::acceptReady(const struct pollfd& event)
{
    const ServerBlock* settings = NULL;
    for (std::size_t i = 0; i < _listeners.size(); ++i)
        if (_listeners[i].fd == event.fd)
            settings = _listeners[i].settings;
    if (!settings)
        throw std::invalid_argument("acceptReady requires a registered listener");
    if (!(event.revents & POLLIN) || (event.revents & POLLNVAL))
        return NULL;
    if (_clients.size() >= MAX_CLIENTS)
        return NULL;
    PendingSocket accepted(accept(event.fd, NULL, NULL));
    if (accepted.get() < 0)
        return NULL;
    if (fcntl(accepted.get(), F_SETFL, O_NONBLOCK) < 0)
        setupError("non-blocking client", errno);
    std::auto_ptr<Client> client(new Client(accepted.get(), *settings));
    if (!_clients.insert(std::make_pair(accepted.get(), client.get())).second)
        throw std::logic_error("client descriptor is already registered");
    accepted.release();
    return client.release();
}

Client* Server::getClient(int fd)
{
    std::map<int, Client*>::iterator it = _clients.find(fd);
    return it == _clients.end() ? NULL : it->second;
}

std::size_t Server::getClientCount() const { return _clients.size(); }

void Server::closeClient(int fd)
{
    std::map<int, Client*>::iterator it = _clients.find(fd);
    if (it == _clients.end())
        return;
    close(fd);
    delete it->second;
    _clients.erase(it);
}

void Server::closeAll()
{
    while (!_clients.empty())
        closeClient(_clients.begin()->first);
    for (std::size_t i = 0; i < _listeners.size(); ++i)
        close(_listeners[i].fd);
    _listeners.clear();
}

void Server::processInput(Client& client)
{
    RequestParser::ParseResult result = client.getParser().feed(client.getReadBuffer());
    if (result == RequestParser::PARSE_HEADERS_COMPLETE) {
        const LocationBlock* location = client.getServerBlock().matchLocation(client.getRequest().uri);
        client.getParser().setBodyLimit(location ? location->getClientMaxBodySize()
                                                : client.getServerBlock().getClientMaxBodySize());
        result = client.getParser().feed(client.getReadBuffer());
    }
    if (result == RequestParser::PARSE_ERROR) {
        client.setCloseAfterResponse(true);
        ResponseBuilder::buildErrorResponse(client, client.getParser().getErrorCode(), &_config);
    } else if (result == RequestParser::PARSE_COMPLETE) {
        client.setState(STATE_BUILDING_RESPONSE);
        ResponseBuilder::buildResponse(client, _config);
    }
}

void Server::readClient(Client& client)
{
    char data[16384];
    ssize_t received = recv(client.getFd(), data, sizeof(data), 0);
    // Subject rule: do not inspect errno after socket transfers.
    if (received <= 0) { closeClient(client.getFd()); return; }
    client.markProgress();
    client.getReadBuffer().append(data, static_cast<std::size_t>(received));
    processInput(client);
}

void Server::writeClient(Client& client)
{
    std::size_t remaining = client.getPendingBytes();
    if (remaining) {
        std::size_t amount = remaining < 16384 ? remaining : 16384;
        ssize_t sent = send(client.getFd(), client.getWriteBuffer().data() + client.getSentOffset(), amount, 0);
        if (sent < 0) { closeClient(client.getFd()); return; }
        if (sent == 0) return;
        client.advanceWrite(static_cast<std::size_t>(sent)); client.markProgress();
    }
    if (client.getPendingBytes()) return;
    if (client.getCloseAfterResponse()) { closeClient(client.getFd()); return; }
    client.reset();
    if (!client.getReadBuffer().empty()) processInput(client);
}

void Server::run()
{
    if (_listeners.empty()) throw std::logic_error("initialize listeners before run");
    SignalPolicy signals;
    std::time_t pauseAccept = 0;
    while (!stopping) {
        std::vector<pollfd> events;
        std::time_t now = std::time(NULL);
        for (std::size_t i = 0; i < _listeners.size(); ++i) {
            pollfd event;
            event.fd = _listeners[i].fd;
            event.events = _clients.size() < MAX_CLIENTS && now >= pauseAccept ? POLLIN : 0;
            event.revents = 0; events.push_back(event);
        }
        for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
            pollfd event;
            event.fd = it->first;
            event.events = it->second->getState() == STATE_WRITING_RESPONSE ? POLLOUT : POLLIN;
            event.revents = 0; events.push_back(event);
        }
        int ready = poll(&events[0], events.size(), 200);
        if (ready < 0) {
            if (errno == EINTR) continue;
            setupError("poll", errno);
        }
        if (stopping) break;
        for (std::size_t i = 0; i < events.size(); ++i) {
            const pollfd& event = events[i];
            if (!event.revents) continue;
            if (i < _listeners.size()) {
                if (event.revents & (POLLERR | POLLHUP | POLLNVAL))
                    throw std::runtime_error("listening socket failed");
                if (event.revents & POLLIN) {
                    try { if (!acceptReady(event)) pauseAccept = std::time(NULL) + 1; }
                    catch (const std::exception&) { pauseAccept = std::time(NULL) + 1; }
                }
                continue;
            }
            Client* client = getClient(event.fd);
            if (!client) continue;
            if (event.revents & (POLLERR | POLLNVAL)) { closeClient(event.fd); continue; }
            try {
                if (client->getState() == STATE_READING_REQUEST && (event.revents & POLLIN))
                    readClient(*client);
                else if (client->getState() == STATE_WRITING_RESPONSE && (event.revents & POLLOUT))
                    writeClient(*client);
                else if (event.revents & POLLHUP) closeClient(event.fd);
            } catch (const std::exception&) { closeClient(event.fd); }
        }
        now = std::time(NULL);
        for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end();) {
            int fd = it->first; Client* client = it->second; ++it;
            if (client->expired(now)) closeClient(fd);
        }
    }
    closeAll();
}
