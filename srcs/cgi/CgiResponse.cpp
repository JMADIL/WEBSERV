#include "CgiResponse.hpp"
#include "Client.hpp"
#include "Config.hpp"
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
typedef std::pair<std::string, std::string> Field;
typedef std::vector<Field> Fields;

bool errorDocument(const std::string& path, std::string& body)
{
    if (path.empty()) return false;
    struct File {
        int fd;
        File() : fd(-1) {}
        ~File() { if (fd >= 0) close(fd); }
    } file;
    // As with scripts, configured roots must not be renamed by an untrusted writer.
    std::size_t start = path[0] == '/' ? 1 : 0;
    while (true) {
        std::size_t end = path.find('/', start);
        std::string prefix = path.substr(0, end);
        if (!prefix.empty()) {
            file.fd = open(prefix.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW
                           | (end == std::string::npos ? 0 : O_DIRECTORY));
            if (file.fd < 0) return false;
            if (end != std::string::npos) { close(file.fd); file.fd = -1; }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    struct stat info;
    if (stat(path.c_str(), &info) < 0 || !S_ISREG(info.st_mode)
        || info.st_size < 0 || info.st_size > 1024 * 1024) return false;
    std::string content;
    char data[16384];
    while (true) {
        ssize_t n = read(file.fd, data, sizeof(data)); // Verified regular files are poll-exempt.
        if (n < 0) return false;
        if (!n) { body.swap(content); return true; }
        if (static_cast<std::size_t>(n) > 1024u * 1024u - content.size()) return false;
        content.append(data, static_cast<std::size_t>(n));
    }
}
std::string lower(std::string value)
{
    for (std::size_t i = 0; i < value.size(); ++i)
        if (value[i] >= 'A' && value[i] <= 'Z') value[i] += 'a' - 'A';
    return value;
}
std::string trim(const std::string& value)
{
    std::size_t start = value.find_first_not_of(" \t");
    return start == std::string::npos ? "" : value.substr(start, value.find_last_not_of(" \t") - start + 1);
}
bool token(const std::string& value)
{
    if (value.empty()) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        unsigned char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
              || std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos)) return false;
    }
    return true;
}
void validateValue(const std::string& value)
{
    for (std::size_t i = 0; i < value.size(); ++i)
        if ((static_cast<unsigned char>(value[i]) < 32 && value[i] != '\t') || value[i] == 127)
            throw std::invalid_argument("invalid CGI header value");
}
std::size_t number(const std::string& value)
{
    if (value.empty()) throw std::invalid_argument("empty CGI length");
    std::size_t result = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        unsigned digit = static_cast<unsigned>(value[i] - '0');
        if (digit > 9 || result > (std::numeric_limits<std::size_t>::max() - digit) / 10)
            throw std::invalid_argument("invalid CGI length");
        result = result * 10 + digit;
    }
    return result;
}
bool absoluteUri(const std::string& value)
{
    std::size_t colon = value.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == value.size()) return false;
    for (std::size_t i = 0; i < colon; ++i) {
        char c = value[i];
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!letter && (i == 0 || !((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')))
            return false;
    }
    return true;
}
struct Parsed {
    int status;
    std::string reason;
    Fields fields;
    std::size_t body;
    std::string localPath;
    std::string localQuery;
    Parsed() : status(200), body(0) {}
};
int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
void localTarget(const std::string& location, Parsed& parsed)
{
    if (location.find('#') != std::string::npos) throw std::invalid_argument("fragment on CGI local redirect");
    std::size_t query = location.find('?');
    std::string raw = location.substr(0, query), decoded;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = raw[i];
        if (c == '%') {
            if (i + 2 >= raw.size() || hexDigit(raw[i + 1]) < 0 || hexDigit(raw[i + 2]) < 0)
                throw std::invalid_argument("bad CGI local redirect escape");
            c = static_cast<unsigned char>(hexDigit(raw[i + 1]) * 16 + hexDigit(raw[i + 2]));
            i += 2;
        }
        if (c < 32 || c == 127 || c == '\\' || c == '?' || c == '#')
            throw std::invalid_argument("unsafe CGI local redirect");
        decoded += c;
    }
    parsed.localPath = "/";
    std::size_t start = 1;
    while (start <= decoded.size()) {
        std::size_t end = decoded.find('/', start);
        std::string part = decoded.substr(start, end - start);
        if (part == "..") throw std::invalid_argument("CGI local redirect traversal");
        if (!part.empty() && part != ".") {
            if (parsed.localPath.size() > 1) parsed.localPath += '/';
            parsed.localPath += part;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (decoded.size() > 1 && decoded[decoded.size() - 1] == '/' && parsed.localPath.size() > 1)
        parsed.localPath += '/';
    parsed.localQuery = query == std::string::npos ? "" : location.substr(query + 1);
}
Parsed parse(const std::string& output)
{
    Parsed parsed;
    std::set<std::string> seen, hop;
    std::string type, location;
    bool explicitStatus = false, hasLength = false;
    std::size_t length = 0, at = 0, fieldCount = 0;
    while (true) {
        std::size_t end = output.find('\n', at);
        if (end == std::string::npos || end + 1 > 32768)
            throw std::invalid_argument("missing or oversized CGI headers");
        std::string line = output.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) { parsed.body = at; break; }
        std::size_t colon = line.find(':');
        if (colon == std::string::npos || !token(line.substr(0, colon)))
            throw std::invalid_argument("invalid CGI header name");
        std::string name = line.substr(0, colon), key = lower(name), value = trim(line.substr(colon + 1));
        validateValue(value);
        ++fieldCount;
        if (key == "status" || key == "content-type" || key == "location" || key == "content-length") {
            if (!seen.insert(key).second) throw std::invalid_argument("duplicate CGI control header");
        }
        if (key == "status") {
            if (value.size() < 3 || (value.size() > 3 && value[3] != ' '))
                throw std::invalid_argument("invalid CGI status");
            parsed.status = static_cast<int>(number(value.substr(0, 3)));
            if (parsed.status < 200 || parsed.status > 599) throw std::invalid_argument("invalid CGI status");
            parsed.reason = value.size() > 3 ? trim(value.substr(4)) : "";
            explicitStatus = true;
        } else if (key == "content-length") { length = number(value); hasLength = true; }
        else if (key == "transfer-encoding") throw std::invalid_argument("CGI transfer encoding is unsupported");
        else if (key == "connection") {
            std::size_t start = 0;
            while (start <= value.size()) {
                std::size_t comma = value.find(',', start);
                std::string item = lower(trim(value.substr(start, comma - start)));
                if (!item.empty()) {
                    if (!token(item) || item == "content-length" || item == "content-type"
                        || item == "status" || item == "location" || item == "set-cookie")
                        throw std::invalid_argument("invalid CGI connection option");
                    hop.insert(item);
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else {
            if (key == "content-type") type = value;
            if (key == "location") location = value;
            if (!((key == "content-type" || key == "location") && value.empty()))
                parsed.fields.push_back(Field(name, value));
        }
    }
    std::size_t bytes = output.size() - parsed.body;
    if (!location.empty()) {
        for (std::size_t i = 0; i < location.size(); ++i)
            if (static_cast<unsigned char>(location[i]) <= 32) throw std::invalid_argument("invalid CGI location");
        if (location[0] != '/' && !absoluteUri(location)) throw std::invalid_argument("invalid CGI location");
        if (!explicitStatus && location[0] == '/') {
            if (fieldCount != 1 || bytes) throw std::invalid_argument("extra data on CGI local redirect");
            localTarget(location, parsed);
            return parsed;
        }
        if (!explicitStatus) parsed.status = 302;
        if (parsed.status != 201 && (parsed.status < 300 || parsed.status > 399))
            throw std::invalid_argument("invalid CGI redirect status");
    }
    bool bodyless = parsed.status == 204 || parsed.status == 205 || parsed.status == 304;
    if (bodyless && bytes) throw std::invalid_argument("body on a bodyless CGI status");
    if (hasLength && parsed.status != 304 && length != bytes)
        throw std::invalid_argument("CGI length does not match its body");
    if (type.empty() && !bodyless && (location.empty() || bytes))
        throw std::invalid_argument("CGI document lacks Content-Type");
    hop.insert("connection"); hop.insert("keep-alive"); hop.insert("proxy-connection"); hop.insert("proxy-authenticate");
    hop.insert("proxy-authorization"); hop.insert("te"); hop.insert("trailer"); hop.insert("upgrade");
    Fields filtered;
    for (Fields::const_iterator it = parsed.fields.begin(); it != parsed.fields.end(); ++it)
        if (!hop.count(lower(it->first))) filtered.push_back(*it);
    parsed.fields.swap(filtered);
    return parsed;
}

void queue(Client& client, int status, const std::string& reason, Fields fields,
           const std::string& output, std::size_t body)
{
    if (client.getState() != STATE_BUILDING_RESPONSE || client.getSentOffset() != 0)
        throw std::logic_error("CGI callback requires an unsent response under construction");
    HttpResponse& response = client.getResponse();
    // Preserve a session cookie already set by the HTTP owner before CGI dispatch.
    Fields cookies;
    for (std::map<std::string, std::string>::const_iterator it = response.headers.begin();
         it != response.headers.end(); ++it)
        if (lower(it->first) == "set-cookie") {
            validateValue(it->second);
            cookies.push_back(*it);
        }
    fields.insert(fields.begin(), cookies.begin(), cookies.end());
    response.reset(); response.setStatus(status);
    if (!reason.empty()) response.statusText = reason;
    bool noLength = status == 204 || status == 304;
    bool noBody = noLength || status == 205 || client.getRequest().method == "HEAD";
    std::size_t bytes = output.size() - body;
    if (!noBody) response.body.assign(output.begin() + body, output.end());
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << ' ' << response.statusText << "\r\n";
    for (Fields::const_iterator it = fields.begin(); it != fields.end(); ++it) {
        response.setHeader(it->first, it->second);
        headers << it->first << ": " << it->second << "\r\n";
    }
    if (!noLength) {
        std::ostringstream length; length << bytes;
        response.setHeader("Content-Length", length.str());
        headers << "Content-Length: " << bytes << "\r\n";
    }
    std::string connection = client.getCloseAfterResponse() ? "close" : "keep-alive";
    response.setHeader("Connection", connection);
    headers << "Connection: " << connection << "\r\n\r\n";
    // Serialize the CGI fields here: HttpResponse's map cannot preserve repeated cookies.
    std::string wire = headers.str();
    if (!noBody) wire.append(output, body, bytes);
    client.getWriteBuffer().swap(wire);
    client.setState(STATE_WRITING_RESPONSE);
}
}

void CgiResponse::onCgiComplete(Client& client, const std::string& output, int waitStatus)
{
    if (!WIFEXITED(waitStatus) || WEXITSTATUS(waitStatus) != 0) { onCgiError(client, 502); return; }
    try {
        Parsed parsed = parse(output);
        if (!parsed.localPath.empty()) client.requestCgiRedirect(parsed.localPath, parsed.localQuery);
        else queue(client, parsed.status, parsed.reason, parsed.fields, output, parsed.body);
    } catch (const std::invalid_argument&) { onCgiError(client, 502); }
}

void CgiResponse::onCgiError(Client& client, int httpStatus)
{
    if (httpStatus < 400 || httpStatus > 599) httpStatus = 500;
    HttpResponse description;
    description.setStatus(httpStatus);
    if (httpStatus == 503) description.statusText = "Service Unavailable";
    Fields fields;
    std::string body = description.statusText + "\n";
    bool custom = errorDocument(Config::resolveErrorPage(client.getServerBlock(), httpStatus), body);
    fields.push_back(Field("Content-Type", custom ? "text/html" : "text/plain"));
    if (httpStatus == 405) {
        const LocationBlock* location = client.getServerBlock().matchLocation(client.getRequest().uri);
        if (location) {
            std::string allow;
            for (std::size_t i = 0; i < location->getAllowedMethods().size(); ++i) {
                if (i) allow += ", ";
                allow += location->getAllowedMethods()[i];
            }
            fields.push_back(Field("Allow", allow));
        }
    }
    queue(client, httpStatus, description.statusText, fields, body, 0);
}
