#include "RequestParserAdapter.hpp"
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
const std::size_t lineLimit = 8192;
const std::size_t headerLimit = 32768;

std::string lower(std::string value)
{
    for (std::size_t i = 0; i < value.size(); ++i)
        if (value[i] >= 'A' && value[i] <= 'Z') value[i] += 'a' - 'A';
    return value;
}
std::string trim(const std::string& value)
{
    std::size_t begin = value.find_first_not_of(" \t");
    if (begin == std::string::npos) return "";
    return value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
}
bool token(const std::string& value)
{
    if (value.empty()) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        unsigned char c = value[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z')
            && !(c >= '0' && c <= '9') && std::string("!#$%&'*+-.^_`|~").find(c) == std::string::npos)
            return false;
    }
    return true;
}
bool field(const std::string& line, std::string& name, std::string& value)
{
    std::size_t colon = line.find(':');
    if (colon == std::string::npos || !token(line.substr(0, colon))) return false;
    for (std::size_t i = colon + 1; i < line.size(); ++i) {
        unsigned char c = line[i];
        if ((c < 32 && c != '\t') || c == 127) return false;
    }
    name = lower(line.substr(0, colon)); value = trim(line.substr(colon + 1));
    return true;
}
int digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool number(const std::string& text, unsigned int base, std::size_t& value)
{
    if (text.empty()) return false;
    value = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        int d = digit(text[i]);
        if (d < 0 || static_cast<unsigned int>(d) >= base
            || value > (std::numeric_limits<std::size_t>::max() - d) / base) return false;
        value = value * base + d;
    }
    return true;
}
bool normalizePath(const std::string& raw, std::string& path)
{
    if (raw.empty() || raw[0] != '/') return false;
    std::string decoded;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = raw[i];
        if (c == '%') {
            if (i + 2 >= raw.size() || digit(raw[i + 1]) < 0 || digit(raw[i + 2]) < 0) return false;
            c = digit(raw[i + 1]) * 16 + digit(raw[i + 2]); i += 2;
        }
        if (c < 32 || c == 127 || c == '\\' || c == '?' || c == '#') return false;
        decoded += c;
    }
    path = "/";
    std::size_t start = 1;
    while (start <= decoded.size()) {
        std::size_t end = decoded.find('/', start);
        std::string segment = decoded.substr(start, end - start);
        if (segment == "..") return false;
        if (!segment.empty() && segment != ".") {
            if (path.size() > 1) path += '/';
            path += segment;
        }
        if (end == std::string::npos) {
            if (segment == "." && path.size() > 1) path += '/';
            break;
        }
        start = end + 1;
    }
    if (decoded[decoded.size() - 1] == '/' && path[path.size() - 1] != '/') path += '/';
    return true;
}
bool closes(const std::string& value)
{
    std::size_t start = 0;
    while (start <= value.size()) {
        std::size_t end = value.find(',', start);
        if (lower(trim(value.substr(start, end - start))) == "close") return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}
}

RequestParserAdapter::RequestParserAdapter() { reset(); }
void RequestParserAdapter::reset()
{
    _parser.reset(); _state = HEADERS; _limit = 0; _headerBytes = 0; _chunkSize = 0; _error = 0;
}
HttpRequest& RequestParserAdapter::getRequest() { return _parser.getRequest(); }
int RequestParserAdapter::getErrorCode() const { return _error; }
RequestParserAdapter::ParseResult RequestParserAdapter::fail(int status)
{ _state = ERROR; _error = status; return PARSE_ERROR; }
void RequestParserAdapter::setBodyLimit(std::size_t bytes)
{
    if (_state != WAIT_LIMIT) throw std::logic_error("set body limit exactly once after headers");
    _limit = bytes;
    if (getRequest().isChunked) _state = CHUNK_SIZE;
    else if (getRequest().contentLength > bytes) fail(413);
    else _state = getRequest().contentLength ? BODY : COMPLETE;
}

RequestParserAdapter::ParseResult RequestParserAdapter::headers(std::string& buffer)
{
    std::size_t first = buffer.find("\r\n");
    if ((first == std::string::npos && buffer.size() > lineLimit) || (first > lineLimit && first != std::string::npos))
        return fail(414);
    std::size_t end = buffer.find("\r\n\r\n");
    std::size_t size = end == std::string::npos ? buffer.size() : end + 4;
    // The line and the aggregate field section have independent bounds.
    if (first != std::string::npos && size - first - 2 > headerLimit) return fail(431);
    for (std::size_t i = 0; i < size; ++i) {
        if ((buffer[i] == '\n' && (i == 0 || buffer[i - 1] != '\r'))
            || (buffer[i] == '\r' && i + 1 < size && buffer[i + 1] != '\n')) return fail(400);
    }
    if (end == std::string::npos) return PARSE_INCOMPLETE;
    std::string line = buffer.substr(0, first);
    std::size_t a = line.find(' '), b = a == std::string::npos ? a : line.find(' ', a + 1);
    if (a == std::string::npos || b == std::string::npos || line.find(' ', b + 1) != std::string::npos)
        return fail(400);
    std::string method = line.substr(0, a), uri = line.substr(a + 1, b - a - 1);
    if (!token(method)) return fail(400);
    if (method != "GET" && method != "POST" && method != "DELETE") return fail(501);
    if (line.substr(b + 1) != "HTTP/1.1") return fail(505);
    for (std::size_t i = 0; i < uri.size(); ++i)
        if (static_cast<unsigned char>(uri[i]) <= 32 || uri[i] == 127 || uri[i] == '#') return fail(400);
    std::string path;
    if (!normalizePath(uri.substr(0, uri.find('?')), path)) return fail(400);
    std::map<std::string, std::string> fields;
    std::size_t start = first + 2;
    while (start < end) {
        std::size_t stop = buffer.find("\r\n", start);
        std::string name, value;
        if (!field(buffer.substr(start, stop - start), name, value)) return fail(400);
        if (fields.count(name)) {
            if (name == "host" || name == "content-length" || name == "transfer-encoding") return fail(400);
            fields[name] += name == "cookie" ? "; " : ", "; fields[name] += value;
        } else fields[name] = value;
        start = stop + 2;
    }
    if (!fields.count("host") || fields["host"].empty()
        || fields["host"].find_first_of(" \t,/@\\") != std::string::npos) return fail(400);
    if (fields.count("content-length") && fields.count("transfer-encoding")) return fail(400);
    std::size_t length = 0;
    if (fields.count("content-length") && !number(fields["content-length"], 10, length)) return fail(400);
    if (fields.count("transfer-encoding") && lower(fields["transfer-encoding"]) != "chunked") return fail(501);
    if (fields.count("expect")) return fail(417);
    // Only hand complete bounded records to the imported parser, never partial framing.
    std::string block = buffer.substr(0, size);
    RequestParser::ParseResult result = _parser.feed(block);
    if (result == RequestParser::PARSE_ERROR || !block.empty()) return fail(400);
    HttpRequest& request = getRequest();
    request.uri = path; request.headers = fields;
    if (closes(request.getHeader("connection"))) request.headers["connection"] = "close";
    _headerBytes = size - first - 2;
    buffer.erase(0, size); _state = WAIT_LIMIT;
    return PARSE_HEADERS_COMPLETE;
}

RequestParserAdapter::ParseResult RequestParserAdapter::feed(std::string& buffer)
{
    if (_state == HEADERS) return headers(buffer);
    if (_state == WAIT_LIMIT) throw std::logic_error("Core must select a body limit before reading the body");
    while (_state != COMPLETE && _state != ERROR) {
        if (_state == BODY) {
            std::size_t remaining = getRequest().contentLength - getRequest().body.size();
            std::size_t amount = buffer.size() < remaining ? buffer.size() : remaining;
            if (!amount) return PARSE_INCOMPLETE;
            std::string data = buffer.substr(0, amount);
            std::size_t before = getRequest().body.size();
            RequestParser::ParseResult result = _parser.feed(data);
            if (result == RequestParser::PARSE_ERROR || !data.empty()
                || getRequest().body.size() - before != amount) return fail(400);
            buffer.erase(0, amount);
            if (getRequest().body.size() == getRequest().contentLength) _state = COMPLETE;
        } else if (_state == CHUNK_SIZE || _state == TRAILERS) {
            std::size_t end = buffer.find("\r\n");
            std::size_t available = end == std::string::npos ? buffer.size() : end + 2;
            if (available > (_state == TRAILERS ? headerLimit - _headerBytes : lineLimit)) return fail(431);
            if (end == std::string::npos) return PARSE_INCOMPLETE;
            if (_state == CHUNK_SIZE) {
                if (!number(buffer.substr(0, end), 16, _chunkSize)) return fail(400);
                if (_chunkSize > _limit - getRequest().body.size()) return fail(413);
                buffer.erase(0, end + 2);
                _state = _chunkSize ? CHUNK_DATA : TRAILERS;
            } else {
                if (end) {
                    std::string name, value;
                    if (!field(buffer.substr(0, end), name, value)
                        || name == "host" || name == "content-length" || name == "transfer-encoding"
                        || name == "connection" || name == "content-type" || name == "authorization"
                        || name == "cookie" || name == "expect" || name == "trailer") return fail(400);
                }
                _headerBytes += end + 2; buffer.erase(0, end + 2);
                if (!end) {
                    // Avoid the backend's split-zero-chunk loop and keep trailers out of routing metadata.
                    std::string finalChunk = "0\r\n\r\n";
                    if (_parser.feed(finalChunk) != RequestParser::PARSE_COMPLETE || !finalChunk.empty()) return fail(400);
                    _state = COMPLETE;
                }
            }
        } else if (_state == CHUNK_DATA) {
            std::size_t amount = buffer.size() < _chunkSize ? buffer.size() : _chunkSize;
            if (!amount) return PARSE_INCOMPLETE;
            if (amount > 16384) amount = 16384;
            // Reframe bounded fragments so the backend never buffers an entire large wire chunk.
            std::ostringstream size;
            size << std::hex << amount << "\r\n";
            std::string data = size.str() + buffer.substr(0, amount) + "\r\n";
            std::size_t before = getRequest().body.size();
            if (_parser.feed(data) != RequestParser::PARSE_INCOMPLETE || !data.empty()
                || getRequest().body.size() - before != amount) return fail(400);
            buffer.erase(0, amount); _chunkSize -= amount;
            if (!_chunkSize) _state = CHUNK_END;
        } else if (_state == CHUNK_END) {
            if (buffer.size() < 2) return PARSE_INCOMPLETE;
            if (buffer.compare(0, 2, "\r\n") != 0) return fail(400);
            buffer.erase(0, 2); _state = CHUNK_SIZE;
        }
    }
    return _state == COMPLETE ? PARSE_COMPLETE : PARSE_ERROR;
}
