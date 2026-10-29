#ifndef REQUESTPARSER_HPP
# define REQUESTPARSER_HPP

# include "HttpRequest.hpp"
# include <string>

class RequestParser
{
    public:
        enum ParseResult
        {
            PARSE_INCOMPLETE,   // Need more data
            PARSE_COMPLETE,     // Full request parsed
            PARSE_ERROR         // Malformed request
        };

        RequestParser();
        ~RequestParser();

        ParseResult feed(std::string &buffer);

        HttpRequest &getRequest();
        void reset();
    
    private:
        enum State
        {
            STATE_REQUEST_LINE,
            STATE_HEADERS,
            STATE_BODY,
            STATE_CHUNKED_BODY,
            STATE_COMPLETE,
            STATE_ERROR
        };

        State       _state;
        HttpRequest _request;
        size_t      _bodyBytesRead;
        size_t      _chunkSize;
        bool        _chunkSizeParsed;

        ParseResult _parseRequestLine(std::string &buffer);
        ParseResult _parseHeaders(std::string &buffer);
        ParseResult _parseBody(std::string &buffer);
        ParseResult _parseChunkedBody(std::string &buffer);

        bool        _isValidMethod(const std::string &method);
        bool        _isValidURI(const std::string &uri);
        bool        _isValidVersion(const std::string &version);

        std::string _trim(const std::string &str);
        std::string _toLower(const std::string &str);
        std::string _decodeURI(const std::string &uri);
};

#endif
