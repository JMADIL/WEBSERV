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
#include <sys/wait.h>

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

Server::Server(const Config& config, const CoreLimits& limits)
    : _config(config), _limits(limits), _insideCgiCallback(false), _running(false)
{
    if (!limits.maxClients || limits.maxClients > MAX_CLIENTS
        || !limits.maxListeners || limits.maxListeners > 1024
        || !limits.maxCgiJobs || limits.maxCgiJobs > MAX_CLIENTS
        || limits.bufferBudget < 65536 || !limits.maxCgiOutput
        || limits.maxCgiOutput > limits.bufferBudget)
        throw std::invalid_argument("invalid core resource limits");
}
Server::~Server() { closeAll(); }

void Server::initializeListeners()
{
    if (_insideCgiCallback) throw std::logic_error("CGI callbacks cannot initialize listeners");
    if (!_listeners.empty())
        throw std::logic_error("listeners are already initialized");
    const std::vector<ServerBlock>& servers = _config.getServers();
    if (servers.empty())
        throw std::runtime_error("cannot start without a loaded configuration");
    if (servers.size() > _limits.maxListeners)
        throw std::runtime_error("configured listener count exceeds core limit");
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
    if (_insideCgiCallback) throw std::logic_error("CGI callbacks cannot accept clients");
    const ServerBlock* settings = NULL;
    for (std::size_t i = 0; i < _listeners.size(); ++i)
        if (_listeners[i].fd == event.fd)
            settings = _listeners[i].settings;
    if (!settings)
        throw std::invalid_argument("acceptReady requires a registered listener");
    if (!(event.revents & POLLIN) || (event.revents & POLLNVAL))
        return NULL;
    if (_clients.size() >= _limits.maxClients || overBudget())
        return NULL;
    sockaddr_in peer;
    socklen_t peerSize = sizeof(peer);
    PendingSocket accepted(accept(event.fd, reinterpret_cast<sockaddr*>(&peer), &peerSize));
    if (accepted.get() < 0)
        return NULL;
    if (fcntl(accepted.get(), F_SETFL, O_NONBLOCK) < 0)
        setupError("non-blocking client", errno);
    std::auto_ptr<Client> client(new Client(accepted.get(), *settings, this));
    unsigned long ip = ntohl(peer.sin_addr.s_addr);
    std::ostringstream peerAddress;
    peerAddress << ((ip >> 24) & 255) << '.' << ((ip >> 16) & 255)
                << '.' << ((ip >> 8) & 255) << '.' << (ip & 255);
    client->setPeer(peerAddress.str(), ntohs(peer.sin_port));
    if (!_clients.insert(std::make_pair(accepted.get(), client.get())).second)
        throw std::logic_error("client descriptor is already registered");
    accepted.release();
    Client* stored = client.release();
    if (overBudget()) { closeClient(stored->getFd()); return NULL; }
    return stored;
}

Client* Server::getClient(int fd)
{
    std::map<int, Client*>::iterator it = _clients.find(fd);
    return it == _clients.end() ? NULL : it->second;
}

std::size_t Server::getClientCount() const { return _clients.size(); }

void Server::closeClient(int fd)
{
    if (_insideCgiCallback) throw std::logic_error("CGI callbacks cannot remove clients");
    std::list<CgiJob>::iterator job = findCgi(fd);
    if (job != _cgiJobs.end()) cancelCgi(job);
    std::map<int, Client*>::iterator it = _clients.find(fd);
    if (it == _clients.end())
        return;
    close(fd);
    delete it->second;
    _clients.erase(it);
}

void Server::closeAll()
{
    if (_insideCgiCallback) throw std::logic_error("CGI callbacks cannot close the server");
    while (!_clients.empty())
        closeClient(_clients.begin()->first);
    for (std::size_t i = 0; i < _listeners.size(); ++i)
        close(_listeners[i].fd);
    _listeners.clear();
    // No blocking wait: kill canceled children, then reap them when they exit.
    while (!_retiredJobs.empty()) {
        pollCgiChildren();
        if (!_retiredJobs.empty()) poll(NULL, 0, 10);
    }
}

std::list<Server::CgiJob>::iterator Server::findCgi(int clientFd)
{
    std::list<CgiJob>::iterator it = _cgiJobs.begin();
    while (it != _cgiJobs.end() && it->clientFd != clientFd) ++it;
    return it;
}

bool Server::ownsFd(int fd) const
{
    if (_clients.count(fd)) return true;
    for (std::size_t i = 0; i < _listeners.size(); ++i)
        if (_listeners[i].fd == fd) return true;
    for (std::map<int, Client*>::const_iterator it = _clients.begin(); it != _clients.end(); ++it)
        if (it->second->getFileFd() == fd) return true;
    for (std::list<CgiJob>::const_iterator it = _cgiJobs.begin(); it != _cgiJobs.end(); ++it)
        if (it->inputFd == fd || it->outputFd == fd) return true;
    return false;
}

void Server::attachCgi(int clientFd, pid_t pid, int inputFd, int outputFd,
                       CgiCallbacks& callbacks, unsigned int timeoutSeconds,
                       std::size_t maxOutputBytes)
{
    Client* client = getClient(clientFd);
    if (_insideCgiCallback || !client || client->getState() != STATE_BUILDING_RESPONSE || pid <= 0
        || client->getSentOffset() != 0
        || inputFd < 0 || outputFd < 0 || inputFd == outputFd
        || ownsFd(inputFd) || ownsFd(outputFd) || !timeoutSeconds || !maxOutputBytes
        || findCgi(clientFd) != _cgiJobs.end())
        throw std::invalid_argument("invalid CGI handoff");
    if (_cgiJobs.size() + _retiredJobs.size() >= _limits.maxCgiJobs
        || maxOutputBytes > _limits.maxCgiOutput || overBudget())
        throw std::runtime_error("CGI resource limit reached");
    for (std::list<CgiJob>::const_iterator it = _cgiJobs.begin(); it != _cgiJobs.end(); ++it)
        if (it->pid == pid) throw std::invalid_argument("CGI child already registered");
    for (std::list<CgiJob>::const_iterator it = _retiredJobs.begin(); it != _retiredJobs.end(); ++it)
        if (it->pid == pid) throw std::invalid_argument("CGI child pending cleanup");
    if (fcntl(inputFd, F_SETFL, O_NONBLOCK) < 0 || fcntl(outputFd, F_SETFL, O_NONBLOCK) < 0)
        setupError("non-blocking CGI pipe", errno);
    CgiJob job;
    job.clientFd = clientFd; job.pid = pid; job.inputFd = inputFd; job.outputFd = outputFd;
    job.inputOffset = 0; job.callbacks = &callbacks; job.started = std::time(NULL);
    job.timeout = timeoutSeconds; job.limit = maxOutputBytes;
    job.exited = false; job.waitStatus = 0; job.errorStatus = 0;
    _cgiJobs.push_back(job);
    client->setState(STATE_CGI_RUNNING);
    client->getWriteBuffer().clear();
    if (client->getRequest().body.empty()) {
        close(_cgiJobs.back().inputFd);
        _cgiJobs.back().inputFd = -1;
    }
}

void Server::cancelCgi(std::list<CgiJob>::iterator job)
{
    if (job->inputFd >= 0) close(job->inputFd);
    if (job->outputFd >= 0) close(job->outputFd);
    job->inputFd = -1; job->outputFd = -1;
    std::string().swap(job->output);
    if (!job->exited) kill(job->pid, SIGKILL);
    // Splice needs no allocation, even when cancellation follows bad_alloc.
    _retiredJobs.splice(_retiredJobs.end(), _cgiJobs, job);
}

void Server::pollCgiChildren()
{
    for (std::list<CgiJob>::iterator it = _cgiJobs.begin(); it != _cgiJobs.end(); ++it) {
        if (it->exited) continue;
        pid_t result = waitpid(it->pid, &it->waitStatus, WNOHANG);
        if (result == it->pid) it->exited = true;
        else if (result < 0 && errno != EINTR) { it->errorStatus = 502; it->exited = true; }
    }
    for (std::list<CgiJob>::iterator it = _retiredJobs.begin(); it != _retiredJobs.end();) {
        pid_t result = it->exited ? it->pid : waitpid(it->pid, &it->waitStatus, WNOHANG);
        if (result == it->pid || (result < 0 && errno != EINTR))
            it = _retiredJobs.erase(it);
        else ++it;
    }
}

void Server::transferCgi(const pollfd& event, int clientFd, bool input)
{
    std::list<CgiJob>::iterator it = findCgi(clientFd);
    if (it == _cgiJobs.end() || it->errorStatus) return;
    int fd = input ? it->inputFd : it->outputFd;
    if (fd != event.fd || fd < 0) return;
    Client* client = getClient(clientFd);
    if (!client) { cancelCgi(it); return; }
    if (event.revents & POLLNVAL) { it->errorStatus = 502; return; }
    if (input) {
        if (event.revents & (POLLERR | POLLHUP)) { it->errorStatus = 502; return; }
        if (!(event.revents & POLLOUT)) return;
        const std::vector<char>& body = client->getRequest().body;
        if (it->inputOffset > body.size()) { it->errorStatus = 502; return; }
        if (it->inputOffset == body.size()) { close(fd); it->inputFd = -1; return; }
        std::size_t remaining = body.size() - it->inputOffset;
        std::size_t amount = remaining < 16384 ? remaining : 16384;
        ssize_t n = write(fd, &body[it->inputOffset], amount);
        if (n < 0) { it->errorStatus = 502; return; }
        if (n > 0) { it->inputOffset += n; client->markProgress(); }
        if (it->inputOffset == body.size()) { close(fd); it->inputFd = -1; }
    } else {
        // Linux pipe hangup may be delivered without POLLIN at empty EOF.
        if (event.revents & POLLIN) {
            char buffer[16384];
            ssize_t n = read(fd, buffer, sizeof(buffer));
            if (n < 0) { it->errorStatus = 502; return; }
            if (n == 0) { close(fd); it->outputFd = -1; return; }
            if (static_cast<std::size_t>(n) > it->limit - it->output.size()) {
                it->errorStatus = 502; return;
            }
            if (overBudget()) { it->errorStatus = 502; std::string().swap(it->output); return; }
            it->output.append(buffer, static_cast<std::size_t>(n)); client->markProgress();
            if (overBudget()) { it->errorStatus = 502; std::string().swap(it->output); }
        } else if (event.revents & POLLHUP) {
            close(fd); it->outputFd = -1;
        } else if (event.revents & POLLERR) it->errorStatus = 502;
    }
}

void Server::finishCgiJobs()
{
    pollCgiChildren();
    std::time_t now = std::time(NULL);
    for (std::list<CgiJob>::iterator it = _cgiJobs.begin(); it != _cgiJobs.end();) {
        std::list<CgiJob>::iterator current = it++;
        bool complete = current->exited && current->outputFd < 0;
        if (!current->errorStatus && !complete
            && std::difftime(now, current->started) >= current->timeout) current->errorStatus = 504;
        if (!current->errorStatus && !complete) continue;
        Client* client = getClient(current->clientFd);
        if (!client) { cancelCgi(current); continue; }
        int fd = current->clientFd;
        bool redirect = false;
        try {
            client->setState(STATE_BUILDING_RESPONSE);
            _insideCgiCallback = true;
            if (current->errorStatus) current->callbacks->onCgiError(*client, current->errorStatus);
            else current->callbacks->onCgiComplete(*client, current->output, current->waitStatus);
            _insideCgiCallback = false;
            redirect = client->hasCgiRedirect();
            if (redirect ? client->getState() != STATE_BUILDING_RESPONSE || !client->getWriteBuffer().empty()
                         : client->getState() != STATE_WRITING_RESPONSE || client->getWriteBuffer().empty())
                throw std::logic_error("CGI callback must queue a response or local redirect");
            if (overBudget()) throw std::runtime_error("CGI response exceeds buffer budget");
            // Give output its own bounded phase after a potentially long CGI job.
            client->beginResponseDeadline();
        } catch (...) {
            _insideCgiCallback = false;
            cancelCgi(current); closeClient(fd); continue;
        }
        cancelCgi(current);
        if (redirect) {
            try {
                // Redispatch only after callback return and the old job's cleanup.
                pollCgiChildren();
                client->applyCgiRedirect();
                buildResponseForClient(*client);
            } catch (const std::exception&) { closeClient(fd); }
        }
    }
}

void Server::processInput(Client& client)
{
    RequestParser::ParseResult result = client.getParser().feed(client.getReadBuffer());
    if (result == RequestParser::PARSE_HEADERS_COMPLETE) {
        const LocationBlock* location = client.getServerBlock().matchLocation(client.getRequest().uri);
        client.getParser().setBodyLimit(location ? location->getClientMaxBodySize()
                                                : client.getServerBlock().getClientMaxBodySize());
        std::size_t used = bufferedBytes();
        std::size_t allowed = location ? location->getClientMaxBodySize()
                                      : client.getServerBlock().getClientMaxBodySize();
        if (!client.getRequest().isChunked && client.getRequest().contentLength <= allowed
            && (used >= _limits.bufferBudget
                || client.getRequest().contentLength > (_limits.bufferBudget - used) / 2)) {
            refuseBusy(client); return;
        }
        result = client.getParser().feed(client.getReadBuffer());
    }
    if (result == RequestParser::PARSE_ERROR) {
        client.setCloseAfterResponse(true);
        const Config requestConfig = _config.forServer(client.getServerBlock());
        ResponseBuilder::buildErrorResponse(client, client.getParser().getErrorCode(), &requestConfig);
    } else if (result == RequestParser::PARSE_COMPLETE) {
        client.setState(STATE_BUILDING_RESPONSE);
        buildResponseForClient(client);
    }
    if (overBudget()) closeClient(client.getFd());
}

void Server::buildResponseForClient(Client& client)
{
    const Config requestConfig = _config.forServer(client.getServerBlock());
    const LocationBlock* location = client.getServerBlock().matchLocation(client.getRequest().uri);
    bool cgi = location && CgiHandler::isCgi(client.getRequest().uri, *location);
    ResponseBuilder::buildResponse(client, requestConfig);
    int status = client.getResponse().statusCode;
    // Some imported file handlers omit Config when rendering errors. Supply the site context here.
    if (!cgi && client.getState() == STATE_WRITING_RESPONSE && status >= 400
        && !requestConfig.getErrorPage(status).empty()) {
        std::map<std::string, std::string> headers = client.getResponse().headers;
        ResponseBuilder::buildErrorResponse(client, status, &requestConfig);
        for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
            std::string name = it->first;
            for (std::size_t i = 0; i < name.size(); ++i)
                if (name[i] >= 'A' && name[i] <= 'Z') name[i] += 'a' - 'A';
            if (name != "content-length" && name != "content-type")
                client.getResponse().setHeader(it->first, it->second);
        }
        client.setCloseAfterResponse(true);
        client.getWriteBuffer() = client.getResponse().serialize();
    }
}

void Server::readClient(Client& client)
{
    char data[16384];
    if (overBudget()) { closeClient(client.getFd()); return; }
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
    if (client.hasFileBody()) {
        client.fillFileChunk();
        if (overBudget()) closeClient(client.getFd());
        return;
    }
    if (client.getCloseAfterResponse()) { closeClient(client.getFd()); return; }
    client.reset();
    if (!client.getReadBuffer().empty()) processInput(client);
}

void Server::run()
{
    if (_running || _insideCgiCallback) throw std::logic_error("Server::run cannot be reentered");
    if (_listeners.empty()) throw std::logic_error("initialize listeners before run");
    struct RunningGuard {
        bool& flag;
        explicit RunningGuard(bool& value) : flag(value) { flag = true; }
        ~RunningGuard() { flag = false; }
    } running(_running);
    SignalPolicy signals;
    std::time_t pauseAccept = 0;
    std::vector<pollfd> events;
    std::vector<int> pipeOwners;
    std::vector<bool> pipeInputs;
    events.reserve(_limits.maxListeners + _limits.maxClients + 2 * _limits.maxCgiJobs);
    pipeOwners.reserve(2 * _limits.maxCgiJobs); pipeInputs.reserve(2 * _limits.maxCgiJobs);
    while (!stopping) {
        events.clear(); pipeOwners.clear(); pipeInputs.clear();
        std::time_t now = std::time(NULL);
        for (std::size_t i = 0; i < _listeners.size(); ++i) {
            pollfd event;
            event.fd = _listeners[i].fd;
            event.events = _clients.size() < _limits.maxClients && !overBudget()
                && now >= pauseAccept ? POLLIN : 0;
            event.revents = 0; events.push_back(event);
        }
        for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
            pollfd event;
            event.fd = it->first;
            event.events = it->second->getState() == STATE_CGI_RUNNING ? 0 :
                it->second->getState() == STATE_WRITING_RESPONSE ? POLLOUT : POLLIN;
            event.revents = 0; events.push_back(event);
        }
        std::size_t socketEvents = events.size();
        for (std::list<CgiJob>::iterator it = _cgiJobs.begin(); it != _cgiJobs.end(); ++it) {
            for (int direction = 0; direction < 2; ++direction) {
                int fd = direction == 0 ? it->inputFd : it->outputFd;
                if (fd < 0 || it->errorStatus) continue;
                pollfd event;
                event.fd = fd; event.events = direction == 0 ? POLLOUT : POLLIN; event.revents = 0;
                events.push_back(event); pipeOwners.push_back(it->clientFd); pipeInputs.push_back(direction == 0);
            }
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
            if (i >= socketEvents) {
                try { transferCgi(event, pipeOwners[i - socketEvents], pipeInputs[i - socketEvents]); }
                catch (...) {
                    std::list<CgiJob>::iterator job = findCgi(pipeOwners[i - socketEvents]);
                    if (job != _cgiJobs.end()) job->errorStatus = 502;
                }
                continue;
            }
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
        finishCgiJobs();
        now = std::time(NULL);
        for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end();) {
            int fd = it->first; Client* client = it->second; ++it;
            if (client->getState() != STATE_CGI_RUNNING && client->expired(now)) closeClient(fd);
        }
    }
    closeAll();
}

std::size_t Server::bufferedBytes() const
{
    std::size_t total = 0;
    for (std::map<int, Client*>::const_iterator it = _clients.begin(); it != _clients.end(); ++it) {
        std::size_t bytes = it->second->bufferedBytes();
        if (bytes > _limits.bufferBudget - total) return _limits.bufferBudget;
        total += bytes;
    }
    for (std::list<CgiJob>::const_iterator it = _cgiJobs.begin(); it != _cgiJobs.end(); ++it) {
        if (it->output.capacity() > _limits.bufferBudget - total) return _limits.bufferBudget;
        total += it->output.capacity();
    }
    return total;
}
bool Server::overBudget() const { return bufferedBytes() >= _limits.bufferBudget; }
void Server::refuseBusy(Client& client)
{
    client.setCloseAfterResponse(true);
    client.getWriteBuffer() = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    client.setState(STATE_WRITING_RESPONSE);
}
