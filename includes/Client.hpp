#ifndef CLIENT_HPP
# define CLIENT_HPP

# include "ServerBlock.hpp"
# include <string>
# include <cstddef>
# include <ctime>
# include "RequestParser.hpp"
# include "HttpResponse.hpp"

enum ClientState
{
    STATE_READING_REQUEST,
    STATE_REQUEST_COMPLETE,
    STATE_BUILDING_RESPONSE,
    STATE_CGI_RUNNING,
    STATE_WRITING_RESPONSE,
    STATE_DONE
};

class Client
{
public:
    Client(int fd, const ServerBlock& server);

    int getFd() const;
    int getListenPort() const;
    const ServerBlock& getServerBlock() const;
    ClientState getState() const;
    void setState(ClientState state);
    std::string& getReadBuffer();
    const std::string& getReadBuffer() const;
    std::string& getWriteBuffer();
    const std::string& getWriteBuffer() const;
    std::size_t getSentOffset() const;
    std::size_t getPendingBytes() const;
    void advanceWrite(std::size_t bytes);
    bool getCloseAfterResponse() const;
    void setCloseAfterResponse(bool close);
    void resetTransport();
    void reset();
    RequestParser& getParser();
    HttpRequest& getRequest();
    HttpResponse& getResponse();
    void markProgress();
    bool expired(std::time_t now) const;

private:
    Client(const Client&);
    Client& operator=(const Client&);

    int _fd;
    const ServerBlock& _server;
    ClientState _state;
    std::string _readBuffer;
    std::string _writeBuffer;
    std::size_t _sentOffset;
    bool _closeAfterResponse;
    RequestParser _parser;
    HttpResponse _response;
    std::time_t _lastProgress;
    std::time_t _requestStart;
};

#endif
