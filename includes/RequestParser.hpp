#ifndef REQUESTPARSER_HPP
# define REQUESTPARSER_HPP
# include "HttpRequest.hpp"
# include <cstddef>
class RequestParser
{
public:
    enum ParseResult { PARSE_INCOMPLETE, PARSE_COMPLETE, PARSE_ERROR, PARSE_HEADERS_COMPLETE };
    RequestParser();
    ParseResult feed(std::string& buffer);
    HttpRequest& getRequest();
    void reset();
    void setBodyLimit(std::size_t bytes);
    int getErrorCode() const;
private:
    enum State { LINE, HEADERS, BODY, CHUNK_SIZE, CHUNK_DATA, CHUNK_END, TRAILERS, COMPLETE, ERROR };
    State _state;
    HttpRequest _request;
    std::size_t _headerBytes;
    std::size_t _remaining;
    std::size_t _bodyLimit;
    int _errorCode;
    ParseResult error(int code);
};
#endif
