#include "Client.hpp"
#include <stdexcept>
#include <unistd.h>
#include <limits>

Client::Client(int fd, const ServerBlock& server, Server* runtime)
    : _fd(fd), _server(server), _runtime(runtime), _remotePort(0), _state(STATE_READING_REQUEST),
      _sentOffset(0), _closeAfterResponse(true),
      _lastProgress(std::time(NULL)), _requestStart(_lastProgress),
      _fileFd(-1), _fileRemaining(0)
{
    if (fd < 0)
        throw std::invalid_argument("Client requires a valid socket descriptor");
}
Client::~Client() { if (_fileFd >= 0) close(_fileFd); }

int Client::getFd() const { return _fd; }
int Client::getListenPort() const { return _server.getPort(); }
const ServerBlock& Client::getServerBlock() const { return _server; }
Server* Client::getRuntimeServer() const { return _runtime; }
const std::string& Client::getRemoteAddress() const { return _remoteAddress; }
unsigned short Client::getRemotePort() const { return _remotePort; }
void Client::setPeer(const std::string& address, unsigned short port)
{ _remoteAddress = address; _remotePort = port; }
ClientState Client::getState() const { return _state; }
void Client::setState(ClientState state) { _state = state; }
std::string& Client::getReadBuffer() { return _readBuffer; }
const std::string& Client::getReadBuffer() const { return _readBuffer; }
std::string& Client::getWriteBuffer() { return _writeBuffer; }
const std::string& Client::getWriteBuffer() const { return _writeBuffer; }
std::size_t Client::getSentOffset() const { return _sentOffset; }
bool Client::getCloseAfterResponse() const { return _closeAfterResponse; }
void Client::setCloseAfterResponse(bool close) { _closeAfterResponse = close; }

std::size_t Client::getPendingBytes() const
{
    if (_sentOffset > _writeBuffer.size())
        throw std::logic_error("write buffer was shortened during a partial send");
    return _writeBuffer.size() - _sentOffset;
}

void Client::advanceWrite(std::size_t bytes)
{
    if (bytes > getPendingBytes())
        throw std::out_of_range("sent byte count exceeds pending output");
    _sentOffset += bytes;
}

void Client::resetTransport()
{
    reset();
}

void Client::reset()
{
    if (_fileFd >= 0) close(_fileFd);
    _fileFd = -1; _fileRemaining = 0;
    std::string().swap(_writeBuffer);
    _sentOffset = 0;
    _closeAfterResponse = true;
    _state = STATE_READING_REQUEST;
    _parser.reset();
    _response.reset();
    std::vector<char>().swap(_parser.getRequest().body);
    std::vector<char>().swap(_response.body);
    _lastProgress = std::time(NULL);
    _requestStart = _lastProgress;
}

RequestParser& Client::getParser() { return _parser; }
HttpRequest& Client::getRequest() { return _parser.getRequest(); }
HttpResponse& Client::getResponse() { return _response; }
void Client::markProgress() { _lastProgress = std::time(NULL); }
void Client::beginResponseDeadline()
{
    _lastProgress = std::time(NULL);
    _requestStart = _lastProgress;
}
bool Client::expired(std::time_t now) const
{
    return std::difftime(now, _lastProgress) >= 10 || std::difftime(now, _requestStart) >= 30;
}

namespace {
void addSize(std::size_t& total, std::size_t value)
{
    std::size_t maximum = std::numeric_limits<std::size_t>::max();
    total = value > maximum - total ? maximum : total + value;
}
}
std::size_t Client::bufferedBytes()
{
    HttpRequest& request = _parser.getRequest();
    std::size_t bytes = sizeof(Client);
    addSize(bytes, _remoteAddress.capacity());
    addSize(bytes, _readBuffer.capacity()); addSize(bytes, _writeBuffer.capacity());
    addSize(bytes, request.body.capacity()); addSize(bytes, _response.body.capacity());
    addSize(bytes, request.method.capacity()); addSize(bytes, request.uri.capacity());
    addSize(bytes, request.querystring.capacity()); addSize(bytes, request.version.capacity());
    addSize(bytes, _response.statusText.capacity());
    for (std::map<std::string, std::string>::const_iterator it = request.headers.begin(); it != request.headers.end(); ++it) {
        addSize(bytes, sizeof(*it) + 3 * sizeof(void*));
        addSize(bytes, it->first.capacity()); addSize(bytes, it->second.capacity());
    }
    for (std::map<std::string, std::string>::const_iterator it = _response.headers.begin(); it != _response.headers.end(); ++it) {
        addSize(bytes, sizeof(*it) + 3 * sizeof(void*));
        addSize(bytes, it->first.capacity()); addSize(bytes, it->second.capacity());
    }
    return bytes;
}
void Client::queueFile(int fd, std::size_t length, const std::string& headers)
{
    if (fd < 0 || fd == _fd || _fileFd >= 0 || _sentOffset != 0
        || _state != STATE_BUILDING_RESPONSE || headers.empty() || headers.size() > 65536)
        throw std::invalid_argument("invalid file response handoff");
    _writeBuffer = headers;
    _fileFd = fd; _fileRemaining = length;
    if (!length) { close(_fileFd); _fileFd = -1; }
    _state = STATE_WRITING_RESPONSE;
}
int Client::getFileFd() const { return _fileFd; }
bool Client::hasFileBody() const { return _fileFd >= 0; }
void Client::fillFileChunk()
{
    if (_fileFd < 0 || getPendingBytes())
        throw std::logic_error("file refill requires drained output");
    char data[16384];
    std::size_t amount = _fileRemaining < sizeof(data) ? _fileRemaining : sizeof(data);
    ssize_t n = read(_fileFd, data, amount); // Verified regular files are poll-exempt.
    if (n <= 0) throw std::runtime_error("file body ended early or could not be read");
    _writeBuffer.assign(data, static_cast<std::size_t>(n)); _sentOffset = 0;
    _fileRemaining -= static_cast<std::size_t>(n);
    if (!_fileRemaining) { close(_fileFd); _fileFd = -1; }
}
