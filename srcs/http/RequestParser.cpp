#include "RequestParser.hpp"
#include <limits>

namespace {
bool token(const std::string& value)
{
    if (value.empty()) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        unsigned char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos))
            return false;
    }
    return true;
}
std::string lower(std::string s)
{
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 'a' - 'A';
    return s;
}
std::string trim(const std::string& s)
{
    std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}
bool number(const std::string& s, unsigned base, std::size_t& value)
{
    if (s.empty()) return false;
    value = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        unsigned digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 :
            c >= 'A' && c <= 'F' ? c - 'A' + 10 : base;
        if (digit >= base || value > (std::numeric_limits<std::size_t>::max() - digit) / base)
            return false;
        value = value * base + digit;
    }
    return true;
}
bool path(const std::string& raw, std::string& normalized)
{
    if (raw.empty() || raw[0] != '/') return false;
    std::string decoded;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = raw[i];
        if (c == '%') {
            std::size_t value;
            if (i + 2 >= raw.size() || !number(raw.substr(i + 1, 2), 16, value)) return false;
            c = static_cast<unsigned char>(value); i += 2;
        }
        if (c < 32 || c == 127 || c == '\\' || c == '#' || c == '?') return false;
        decoded += c;
    }
    normalized = "/";
    std::size_t start = 1;
    while (start <= decoded.size()) {
        std::size_t end = decoded.find('/', start);
        std::string part = decoded.substr(start, end - start);
        if (part == "..") return false;
        if (!part.empty() && part != ".") {
            if (normalized.size() > 1) normalized += '/';
            normalized += part;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (decoded.size() > 1 && decoded[decoded.size() - 1] == '/' && normalized.size() > 1)
        normalized += '/';
    return true;
}
}
RequestParser::RequestParser() { reset(); }
void RequestParser::reset()
{
    _state = LINE; _request.reset(); _headerBytes = 0; _remaining = 0;
    _bodyLimit = 1024u * 1024u; _errorCode = 400;
}
HttpRequest& RequestParser::getRequest() { return _request; }
int RequestParser::getErrorCode() const { return _errorCode; }
RequestParser::ParseResult RequestParser::error(int code)
{ _state = ERROR; _errorCode = code; return PARSE_ERROR; }
void RequestParser::setBodyLimit(std::size_t bytes)
{
    _bodyLimit = bytes;
    if (_request.body.size() > bytes || (!_request.isChunked && _request.contentLength > bytes))
        error(413);
}
RequestParser::ParseResult RequestParser::feed(std::string& buffer)
{
    while (_state != COMPLETE && _state != ERROR) {
        if (_state == BODY || _state == CHUNK_DATA) {
            std::size_t n = buffer.size() < _remaining ? buffer.size() : _remaining;
            if (n > _bodyLimit - _request.body.size()) return error(413);
            _request.body.insert(_request.body.end(), buffer.begin(), buffer.begin() + n);
            buffer.erase(0, n); _remaining -= n;
            if (_remaining) return PARSE_INCOMPLETE;
            _state = _state == BODY ? COMPLETE : CHUNK_END;
            continue;
        }
        if (_state == CHUNK_END) {
            if (buffer.size() < 2) return PARSE_INCOMPLETE;
            if (buffer.compare(0, 2, "\r\n") != 0) return error(400);
            buffer.erase(0, 2); _state = CHUNK_SIZE; continue;
        }
        std::size_t end = buffer.find("\r\n");
        std::size_t limit = _state == LINE ? 8192 : 32768;
        if (end == std::string::npos) {
            if (buffer.size() > limit || (_state != LINE && buffer.size() > 32768 - _headerBytes))
                return error(_state == LINE ? 414 : 431);
            return PARSE_INCOMPLETE;
        }
        if (end > limit) return error(_state == LINE ? 414 : 431);
        std::string line = buffer.substr(0, end); buffer.erase(0, end + 2);
        if (_state == LINE) {
            std::size_t a = line.find(' '), b = line.find(' ', a == std::string::npos ? 0 : a + 1);
            if (a == std::string::npos || b == std::string::npos || line.find(' ', b + 1) != std::string::npos)
                return error(400);
            _request.method = line.substr(0, a); _request.version = line.substr(b + 1);
            if (!token(_request.method)) return error(400);
            if (_request.version != "HTTP/1.1") return error(505);
            std::string target = line.substr(a + 1, b - a - 1);
            for (std::size_t i = 0; i < target.size(); ++i)
                if (static_cast<unsigned char>(target[i]) <= 32 || target[i] == 127 || target[i] == '#')
                    return error(400);
            std::size_t q = target.find('?');
            _request.querystring = q == std::string::npos ? "" : target.substr(q + 1);
            if (!path(target.substr(0, q), _request.uri)) return error(400);
            _state = HEADERS;
        } else if (_state == CHUNK_SIZE) {
            // Chunk extensions are outside this basic parser's supported syntax.
            if (!number(line, 16, _remaining)) return error(400);
            if (_remaining > _bodyLimit - _request.body.size()) return error(413);
            _state = _remaining ? CHUNK_DATA : TRAILERS;
        } else {
            if (end + 2 > 32768 - _headerBytes) return error(431);
            _headerBytes += end + 2;
            if (line.empty()) {
                if (_state == TRAILERS) { _state = COMPLETE; continue; }
                if (!_request.hasHeader("host") || _request.getHeader("host").empty()) return error(400);
                if (_request.hasHeader("expect")) return error(417);
                bool cl = _request.hasHeader("content-length"), te = _request.hasHeader("transfer-encoding");
                if (cl && te) return error(400);
                if (te && lower(_request.getHeader("transfer-encoding")) != "chunked") return error(501);
                _request.isChunked = te;
                if (cl && !number(_request.getHeader("content-length"), 10, _request.contentLength)) return error(400);
                _remaining = _request.contentLength;
                _state = te ? CHUNK_SIZE : _remaining ? BODY : COMPLETE;
                return PARSE_HEADERS_COMPLETE;
            }
            std::size_t colon = line.find(':');
            if (colon == std::string::npos || !token(line.substr(0, colon))) return error(400);
            std::string key = lower(line.substr(0, colon)), value = trim(line.substr(colon + 1));
            for (std::size_t i = 0; i < value.size(); ++i)
                if ((static_cast<unsigned char>(value[i]) < 32 && value[i] != '\t') || value[i] == 127) return error(400);
            if (_state == TRAILERS) {
                if (key == "host" || key == "content-length" || key == "transfer-encoding") return error(400);
                continue;
            }
            if (_request.hasHeader(key)) {
                if (key == "host" || key == "content-length" || key == "transfer-encoding") return error(400);
                _request.headers[key] += "," + value;
            } else _request.headers[key] = value;
        }
    }
    return _state == COMPLETE ? PARSE_COMPLETE : PARSE_ERROR;
}
