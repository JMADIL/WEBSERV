#ifndef REQUESTPARSERADAPTER_HPP
# define REQUESTPARSERADAPTER_HPP
# include "RequestParser.hpp"

// Core owns framing/limits; the unchanged HTTP parser supplies HttpRequest.
class RequestParserAdapter
{
public:
    enum ParseResult { PARSE_INCOMPLETE, PARSE_HEADERS_COMPLETE, PARSE_COMPLETE, PARSE_ERROR };
    RequestParserAdapter();
    ParseResult feed(std::string& buffer);
    HttpRequest& getRequest();
    void setBodyLimit(std::size_t bytes);
    int getErrorCode() const;
    void reset();
private:
    enum State { HEADERS, WAIT_LIMIT, BODY, CHUNK_SIZE, CHUNK_DATA, CHUNK_END, TRAILERS, COMPLETE, ERROR };
    RequestParser _parser;
    State _state;
    std::size_t _limit;
    std::size_t _headerBytes;
    std::size_t _chunkSize;
    int _error;
    ParseResult fail(int status);
    ParseResult headers(std::string& buffer);
};
#endif
