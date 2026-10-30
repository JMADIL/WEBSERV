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

namespace
{
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
