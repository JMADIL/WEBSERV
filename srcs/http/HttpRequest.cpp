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
        size_t start = 0;
        while (start <= conn.size()) {
            size_t end = conn.find(',', start);
            std::string item = conn.substr(start, end - start);
            size_t a = item.find_first_not_of(" \t");
            if (a != std::string::npos && item.substr(a, item.find_last_not_of(" \t") - a + 1) == "close")
                return false;
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    return version == "HTTP/1.1";
}
