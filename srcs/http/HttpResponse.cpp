#include "HttpResponse.hpp"
#include <sstream>

HttpResponse::HttpResponse() : statusCode(200), statusText("OK")
{
}

HttpResponse::~HttpResponse()
{
}

void HttpResponse::setStatus(int code)
{
    statusCode = code;
    statusText = _getStatusText(code);
}

void HttpResponse::setHeader(const std::string &key, const std::string &value)
{
    headers[key] = value;
}

void HttpResponse::setBody(const std::string &content)
{
    body.assign(content.begin(), content.end());
}

void HttpResponse::setBody(const std::vector<char> &content)
{
    body = content;
}

std::string HttpResponse::serialize() const
{
    std::ostringstream oss;

    oss << "HTTP/1.1" << statusCode << " " << statusText << "\r\n";

    bool hasContentLength = false;
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it)
    {
        if (it->first == "Content-Length" || it->first == "content-length")
        {
            hasContentLength = true;
            break;
        }
    }

    if (!hasContentLength)
    {
        oss << "Content-Length: " << body.size() << "\r\n";
    }

    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it)
    {
        oss << it->first << ": " << it->second << "\r\n";
    }
    oss << "\r\n";

    std::string responseString = oss.str();
    if (!body.empty())
    {
        responseString.append(&body[0], body.size());
    }

    return responseString;
}

void HttpResponse::reset()
{
    statusCode = 200;
    statusText = "OK";
    headers.clear();
    body.clear();
}

std::string HttpResponse::_getStatusText(int code)
{
    switch (code)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";

        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";

        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 431: return "Request Header Fields Too Large";

        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";

        default:  return "Unknown Status";
    }
}
