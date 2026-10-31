#include "HttpResponse.hpp"
#include <sstream>

HttpResponse::HttpResponse() { reset(); }
HttpResponse::~HttpResponse() {}
void HttpResponse::reset() { statusCode = 200; statusText = "OK"; headers.clear(); body.clear(); }
void HttpResponse::setStatus(int code) { statusCode = code; statusText = _getStatusText(code); }
void HttpResponse::setHeader(const std::string& key, const std::string& value) { headers[key] = value; }
void HttpResponse::setBody(const std::string& value) { body.assign(value.begin(), value.end()); }
void HttpResponse::setBody(const std::vector<char>& value) { body = value; }
std::string HttpResponse::serialize() const
{
    std::ostringstream out;
    out << "HTTP/1.1 " << statusCode << ' ' << statusText << "\r\n";
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
        std::string key = it->first;
        for (std::size_t i = 0; i < key.size(); ++i)
            if (key[i] >= 'A' && key[i] <= 'Z') key[i] += 'a' - 'A';
        if (key != "content-length" && key != "transfer-encoding")
            out << it->first << ": " << it->second << "\r\n";
    }
    out << "Content-Length: " << body.size() << "\r\n\r\n";
    std::string result = out.str();
    if (!body.empty()) result.append(&body[0], body.size());
    return result;
}
std::string HttpResponse::_getStatusText(int code)
{
    switch (code) {
        case 200: return "OK";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 417: return "Expectation Failed";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 505: return "HTTP Version Not Supported";
        default: return "Error";
    }
}
