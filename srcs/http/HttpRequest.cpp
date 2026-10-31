#include "HttpRequest.hpp"
#include <algorithm>
#include <cctype>

HttpRequest::HttpRequest() : contentLength(0), isChunked(false)
{
}

HttpRequest::~HttpRequest()
{
}

void HttpRequest::reset()
{
    method.clear();
    uri.clear();
    querystring.clear();
    version.clear();
    headers.clear();
    body.clear();
    contentLength = 0;
    isChunked = false;
}

static std::string toLowerString(const std::string &str)
{
    std::string lower = str;
    for(size_t i = 0; i < lower.length(); ++i)
    {
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
    }
    return lower;
}

std::string HttpRequest::getHeader(const std::string &key) const
{
    std::string lowerkey = toLowerString(key);
    std::map<std::string, std::string>::const_iterator it = headers.find(lowerkey);
    if(it != headers.end())
        return it->second;
    return "";
}

bool HttpRequest::hasHeader(const std::string &key) const
{
    std::string lowerkey = toLowerString(key);
    return headers.find(lowerkey) != headers.end();
}

bool HttpRequest::keepAlive() const
{
    if(hasHeader("connection"))
    {
        std::string conn = toLowerString(getHeader("connection"));
        if(conn == "close")
            return false;
        if(conn == "keep-alive")
            return true;
    }

    if (version == "HTTP/1.1")
        return true;
    return false;
}

std::string HttpRequest::getCookie(const std::string &name) const
{
    if (!hasHeader("cookie"))
        return "";

    std::string cookieHeader = getHeader("cookie");
    size_t start = 0;

    while (start < cookieHeader.length())
    {
        while (start < cookieHeader.length() && (cookieHeader[start] == ' ' || cookieHeader[start] == '\t'))
            start++;
        size_t end = cookieHeader.find(';', start);
        if (end == std::string::npos)
            end = cookieHeader.length();

        std::string pair = cookieHeader.substr(start, end - start);
        size_t eqPos = pair.find('=');

        if (eqPos != std::string::npos)
        {
            std::string key = pair.substr(0, eqPos);
            std::string val = pair.substr(eqPos + 1);

            size_t kStart = key.find_first_not_of(" \t");
            size_t kEnd = key.find_last_not_of(" \t");
            if (kStart != std::string::npos)
                key = key.substr(kStart, kEnd - kStart + 1);

            if (key == name)
            {
                size_t vStart = val.find_first_not_of(" \t");
                size_t vEnd = val.find_last_not_of(" \t");
                if (vStart != std::string::npos)
                    return val.substr(vStart, vEnd - vStart + 1);
                return "";
            }
        }
        start = end + 1;
    }

    return "";
}