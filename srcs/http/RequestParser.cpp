#include "RequestParser.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

RequestParser::RequestParser() : _state(STATE_REQUEST_LINE), _bodyBytesRead(0), _chunkSize(0), _chunkSizeParsed(false)
{
}

RequestParser::~RequestParser()
{
}

HttpRequest &RequestParser::getRequest()
{
    return _request;
}

//parsing
RequestParser::ParseResult RequestParser::feed(std::string &buffer)
{
    while(_State != STATE_COMPLETE && _State != STATE_ERROR)
    {
        if(_State == STATE_REQUEST_LINE)
        {

        }
        else if(_State == STATE_HEADERS)
        {
            if()

            if()
        }
        else if (_State == STATE_BODY)
        {

        }
        else if(_State == STATE_COMPLETE)
        {

        }
    }
    return ();
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

    std::string line = buffer.substr(0, pos);
    buffer.erase(0, pos + 2);

    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;

    while(iss >> token)
        tokens.push_back(token);

    if(token.size != 3)
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
        size_t pos = buffer.find("/r/n");
        if(pos == std::string::npos)
        {
            if(buffer.length() > 32768)
            {
                _state = STATE_ERROR;
                return PARSE_ERROR;
            }
            return PARSE_INCOMPLETE;
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

