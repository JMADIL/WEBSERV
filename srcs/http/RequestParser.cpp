#include "RequestParser.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <limits>

RequestParser::RequestParser() : _state(STATE_REQUEST_LINE), _bodyBytesRead(0), _chunkSize(0), _chunkSizeParsed(false), _bodyLimit(std::numeric_limits<size_t>::max()), _errorCode(400)
{
}

RequestParser::~RequestParser()
{
}

HttpRequest &RequestParser::getRequest()
{
    return _request;
}

void RequestParser::reset()
{
    _state = STATE_REQUEST_LINE;
    _bodyBytesRead = 0;
    _chunkSize = 0;
    _chunkSizeParsed = false;
    _bodyLimit = std::numeric_limits<size_t>::max();
    _errorCode = 400;
    _request.reset();
}

//parsing
RequestParser::ParseResult RequestParser::feed(std::string &buffer)
{
    while(_state != STATE_COMPLETE && _state != STATE_ERROR)
    {
        if(_state == STATE_REQUEST_LINE)
        {
            ParseResult res = _parseRequestLine(buffer);
            if (res != PARSE_COMPLETE)
                return res;
        }
        else if(_state == STATE_HEADERS)
        {
            ParseResult res = _parseHeaders(buffer);
            if (res != PARSE_COMPLETE)
                return res;
            return PARSE_HEADERS_COMPLETE;
        }
        else if (_state == STATE_BODY)
        {
            return _parseBody(buffer);
        }
        else if (_state == STATE_CHUNKED_BODY)
        {
            return _parseChunkedBody(buffer);
        }
    }
    return (_state == STATE_COMPLETE) ? PARSE_COMPLETE : PARSE_ERROR;
}

RequestParser::ParseResult RequestParser::_parseRequestLine(std::string &buffer)
{
    size_t pos = buffer.find("\r\n");
    if(pos == std::string::npos)
    {
        if(buffer.length() > 8192)
        {
            _state = STATE_ERROR;
            return PARSE_ERROR;
        }
        return PARSE_INCOMPLETE;
    }

    if (pos == 0)
    {
        buffer.erase(0, 2);
        return PARSE_INCOMPLETE;
    }

    std::string line = buffer.substr(0, pos);
    buffer.erase(0, pos + 2);

    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;

    while(iss >> token)
        tokens.push_back(token);

    if(tokens.size() != 3)
    {
        _state = STATE_ERROR;
        return PARSE_ERROR;
    }

    std::string method = tokens[0];
    std::string rawUri = tokens[1];
    std::string version = tokens[2];

    if(!_isValidMethod(method) || !_isValidVersion(version) || !_isValidURI(rawUri))
    {
        _state = STATE_ERROR;
        return PARSE_ERROR;
    }

    size_t qpos = rawUri.find('?');
    std::string pathOnly = (qpos != std::string::npos) ? rawUri.substr(0, qpos) : rawUri;
    std::string queryString = (qpos != std::string::npos) ? rawUri.substr(qpos + 1) : "";

    std::string decodedPath = _decodeURI(pathOnly);
    if(decodedPath.empty())
    {
        _state = STATE_ERROR;
        return PARSE_ERROR;
    }

    _request.method = method;
    _request.uri = decodedPath;
    _request.querystring = queryString;
    _request.version = version;

    _state = STATE_HEADERS;
    return PARSE_COMPLETE;
}

RequestParser::ParseResult RequestParser::_parseHeaders(std::string &buffer)
{
    while(true)
    {
        size_t pos = buffer.find("\r\n");
        if(pos == std::string::npos)
        {
            if(buffer.length() > 32768)
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }
            return PARSE_INCOMPLETE;
        }

        if(pos == 0)
        {
            buffer.erase(0, 2);
            if(!_request.hasHeader("host"))
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }
            if(_request.hasHeader("transfer-encoding"))
            {
                std::string te = _toLower(_request.getHeader("transfer-encoding"));
                if (te == "chunked")
                {
                    _request.isChunked = true;
                    _state = STATE_CHUNKED_BODY;
                    return PARSE_COMPLETE;
                }
                else
                {
                    _state = STATE_ERROR;
                    return PARSE_ERROR;
                }
            }
            if(_request.hasHeader("content-length"))
            {
                std::string clStr = _request.getHeader("content-length");
                for(size_t i = 0; i < clStr.length(); ++i)
                {
                    if(!std::isdigit(static_cast<unsigned char>(clStr[i])))
                    {
                        _state = STATE_ERROR;
                        return PARSE_ERROR;
                    }

                }

                std::istringstream clStream(clStr);
                size_t length = 0;
                clStream >> length;

                _request.contentLength = length;
                if(length > 0)
                {
                    _state = STATE_BODY;
                    return PARSE_COMPLETE;
                }
            }
            _state = STATE_COMPLETE;
            return PARSE_COMPLETE;
        }

        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 2);

        size_t colon = line.find(':');
        if(colon == std::string::npos)
        {
            _state = STATE_ERROR;
            return PARSE_ERROR;
        }

        if (colon > 0 && (line[colon - 1] == ' ' || line[colon - 1] == '\t'))
        {
            _state = STATE_ERROR;
            return PARSE_ERROR;
        }

        std::string key = _toLower(_trim(line.substr(0, colon)));
        std::string value = _trim(line.substr(colon + 1));

        if (key.empty())
        {
            _state = STATE_ERROR;
            return PARSE_ERROR;
        }

        if (key == "host" && _request.hasHeader("host"))
        {
            _state = STATE_ERROR;
            return PARSE_ERROR;
        }

        _request.headers[key] = value;
    }
}

RequestParser::ParseResult RequestParser::_parseBody(std::string &buffer)
{

    if (_request.contentLength > _bodyLimit)
    {
        _errorCode = 413;
        _state = STATE_ERROR;
        return PARSE_ERROR;
    }

   size_t remaining = _request.contentLength - _bodyBytesRead;
   if(buffer.empty())
   {
        return PARSE_INCOMPLETE;
   }
   size_t toRead = (buffer.size() < remaining) ? buffer.size() : remaining;

   _request.body.insert(_request.body.end(), buffer.begin(), buffer.begin() + toRead);
   buffer.erase(0, toRead);
   _bodyBytesRead += toRead;

    if (_bodyBytesRead >= _request.contentLength)
    {
        _state = STATE_COMPLETE;
        return PARSE_COMPLETE;
    }

    return PARSE_INCOMPLETE;
}

RequestParser::ParseResult RequestParser::_parseChunkedBody(std::string &buffer)
{
    while(true)
    {
        if(!_chunkSizeParsed)
        {
            size_t pos = buffer.find("\r\n");
            if(pos == std::string::npos)
                return PARSE_INCOMPLETE;
            
            std::string line = buffer.substr(0, pos);

            size_t semi = line.find(';');
            if (semi != std::string::npos)
                line = line.substr(0, semi);
            line = _trim(line);
            if(line.empty())
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }
            
            for(size_t i = 0; i < line.length(); ++i)
            {
                if (!std::isxdigit(static_cast<unsigned char>(line[i])))
                {
                    _state = STATE_ERROR;
                    return PARSE_ERROR;
                }
            }

            std::istringstream hexStream(line);
            size_t size = 0;
             if (!(hexStream >> std::hex >> size))
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }

            if (_request.body.size() > _bodyLimit ||
                size > _bodyLimit - _request.body.size())
            {
                _errorCode = 413;
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }

            buffer.erase(0, pos + 2);
            _chunkSize = size;
            _chunkSizeParsed = true;

            if(_chunkSize == 0)
            {
                size_t endPos = buffer.find("\r\n");
                if(endPos == std::string::npos)
                    return PARSE_INCOMPLETE;
                
                buffer.erase(0, endPos + 2);
                _state = STATE_COMPLETE;
                return PARSE_COMPLETE;
            }
        }

        if (_chunkSizeParsed && _chunkSize > 0)
        {
            if (buffer.size() < _chunkSize || buffer.size() - _chunkSize < 2)
                return PARSE_INCOMPLETE;

            if (buffer[_chunkSize] != '\r' || buffer[_chunkSize + 1] != '\n')
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }

            _request.body.insert(_request.body.end(), buffer.begin(), buffer.begin() + _chunkSize);
            buffer.erase(0, _chunkSize + 2);

            _chunkSize = 0;
            _chunkSizeParsed = false;
        }
        else if (_chunkSizeParsed && _chunkSize == 0)
        {
            size_t endPos = buffer.find("\r\n");
            if (endPos == std::string::npos)
                return PARSE_INCOMPLETE;

            buffer.erase(0, endPos + 2);
            _state = STATE_COMPLETE;
            return PARSE_COMPLETE;
        }
    }
}

//validation: 
bool RequestParser::_isValidMethod(const std::string &method)
{
    return (method == "GET" || method == "POST" || method == "DELETE");
}

bool RequestParser::_isValidURI(const std::string &uri)
{
    if (uri.empty() || uri[0] != '/')
        return false;
    return true;
}

bool RequestParser::_isValidVersion(const std::string &version)
{
    return (version == "HTTP/1.1");
}

std::string RequestParser::_trim(const std::string &str)
{
    size_t first = 0;
    while (first < str.length() && (str[first] == ' ' || str[first] == '\t' || str[first] == '\r'))
        first++;

    if (first == str.length())
        return "";

    size_t last = str.length() - 1;
    while (last > first && (str[last] == ' ' || str[last] == '\t' || str[last] == '\r'))
        last--;

    return str.substr(first, last - first + 1);
}

std::string RequestParser::_toLower(const std::string &str)
{
    std::string lower = str;
    for (size_t i = 0; i < lower.length(); ++i)
    {
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
    }
    return lower;
}

std::string RequestParser::_decodeURI(const std::string &uri)
{
    std::string decoded;
    decoded.reserve(uri.length());

    for(size_t i = 0; i < uri.length(); ++i)
    {
        if (uri[i] == '%' && i + 2 < uri.length() &&
            std::isxdigit(static_cast<unsigned char>(uri[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(uri[i + 2])))
        {
            std::string hexStr = uri.substr(i + 1, 2);
            std::istringstream hexStream(hexStr);
            int value = 0;
            hexStream >> std::hex >> value;
            decoded += static_cast<char>(value);
            i += 2;
        }
        else
        {
            decoded += uri[i];
        }
    }

    if (decoded.find("/../") != std::string::npos ||
        decoded == ".." ||
        (decoded.length() >= 3 && decoded.substr(decoded.length() - 3) == "/.."))
        {
            return ""; // Reject path traversal attempt
        }

    return decoded;
}

void RequestParser::setBodyLimit(size_t limit)
{
    _bodyLimit = limit;
    if (_request.body.size() > limit ||
        (!_request.isChunked && _request.contentLength > limit))
    {
        _errorCode = 413;
        _state = STATE_ERROR;
    }
}

int RequestParser::getErrorCode() const
{
    return _errorCode;
}
