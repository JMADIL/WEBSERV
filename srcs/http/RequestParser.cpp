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

