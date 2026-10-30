#include "Client.hpp"
#include <stdexcept>

Client::Client(int fd, const ServerBlock& server)
    : _fd(fd), _server(server), _state(STATE_READING_REQUEST),
      _sentOffset(0), _closeAfterResponse(true)
{
    if (fd < 0)
        throw std::invalid_argument("Client requires a valid socket descriptor");
}

int Client::getFd() const { return _fd; }
int Client::getListenPort() const { return _server.getPort(); }
const ServerBlock& Client::getServerBlock() const { return _server; }
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
    _writeBuffer.clear();
    _sentOffset = 0;
    _closeAfterResponse = true;
    _state = STATE_READING_REQUEST;
}
