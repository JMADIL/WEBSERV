#ifndef CLIENT_HPP
# define CLIENT_HPP

# include "ServerBlock.hpp"
# include <string>
# include <cstddef>
# include <ctime>
# include "RequestParser.hpp"
# include "HttpResponse.hpp"

class Server;

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
    Client(int fd, const ServerBlock& server, Server* runtime = NULL);
    ~Client();

    int getFd() const;
    int getListenPort() const;
    const ServerBlock& getServerBlock() const;
    // CGI uses the live server; ServerBlock only holds configuration.
    Server* getRuntimeServer() const;
    const std::string& getRemoteAddress() const;
    unsigned short getRemotePort() const;
    void setPeer(const std::string& address, unsigned short port);
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
    void beginResponseDeadline();
    bool expired(std::time_t now) const;
    std::size_t bufferedBytes();
    // Caller supplies headers and a verified regular-file fd; success transfers ownership.
    void queueFile(int fd, std::size_t length, const std::string& headers);
    int getFileFd() const;
    bool hasFileBody() const;
    void fillFileChunk();

private:
    Client(const Client&);
    Client& operator=(const Client&);

    int _fd;
    const ServerBlock& _server;
    Server* _runtime;
    std::string _remoteAddress;
    unsigned short _remotePort;
    ClientState _state;
    std::string _readBuffer;
    std::string _writeBuffer;
    std::size_t _sentOffset;
    bool _closeAfterResponse;
    RequestParser _parser;
    HttpResponse _response;
    std::time_t _lastProgress;
    std::time_t _requestStart;
    int _fileFd;
    std::size_t _fileRemaining;
};

#endif
