#include "ResponseBuilder.hpp"
#include "Client.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <sstream>
#include <algorithm>

namespace {
struct File {
    int fd;
    explicit File(int value) : fd(value) {}
    ~File() { if (fd >= 0) close(fd); }
private:
    File(const File&);
    File& operator=(const File&);
};
std::string join(const std::string& root, const std::string& suffix)
{
    if (suffix.empty()) return root;
    return root + (root[root.size() - 1] == '/' || suffix[0] == '/' ? "" : "/") + suffix;
}
int readFile(const std::string& path, std::vector<char>& body)
{
    // Refuse symlinks in all path components. Content roots must not be renamed
    // concurrently by an untrusted writer (openat is outside the allowed API).
    std::size_t start = path[0] == '/' ? 1 : 0;
    while (true) {
        std::size_t end = path.find('/', start);
        std::string prefix = path.substr(0, end);
        if (!prefix.empty()) {
            int flags = O_RDONLY | O_NONBLOCK | O_NOFOLLOW;
            if (end != std::string::npos) flags |= O_DIRECTORY;
            File part(open(prefix.c_str(), flags));
            if (part.fd < 0) return errno == ENOENT ? 404 : 403;
            if (end == std::string::npos) {
                struct stat info;
                if (stat(path.c_str(), &info) < 0 || !S_ISREG(info.st_mode)) return 403;
                const std::size_t maxFile = 8u * 1024u * 1024u;
                if (info.st_size < 0 || info.st_size > static_cast<off_t>(maxFile)) return 503;
                body.clear();
                char bytes[16384];
                while (true) {
                    ssize_t n = read(part.fd, bytes, sizeof(bytes));
                    if (n < 0) return 500;
                    if (n == 0) return 200;
                    if (static_cast<std::size_t>(n) > maxFile - body.size()) return 503;
                    body.insert(body.end(), bytes, bytes + n);
                }
            }
        }
        start = end + 1;
    }
}
std::string mime(const std::string& path)
{
    std::size_t dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".txt") return "text/plain";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}
std::string encodePath(const std::string& path)
{
    const char* hex = "0123456789ABCDEF";
    std::string result;
    for (std::size_t i = 0; i < path.size(); ++i) {
        unsigned char c = path[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') result += c;
        else { result += '%'; result += hex[c / 16]; result += hex[c % 16]; }
    }
    return result;
}
void finish(Client& client)
{
    client.getResponse().setHeader("Connection", client.getCloseAfterResponse() ? "close" : "keep-alive");
    client.getWriteBuffer() = client.getResponse().serialize();
    client.setState(STATE_WRITING_RESPONSE);
}
}

void ResponseBuilder::buildErrorResponse(Client& client, int status, const Config* config)
{
    (void)config;
    HttpResponse& response = client.getResponse();
    response.reset(); response.setStatus(status);
    response.setHeader("Content-Type", "text/html");
    std::string errorPage = client.getServerBlock().getErrorPage(status);
    const LocationBlock* root = client.getServerBlock().matchLocation("/");
    if (!errorPage.empty() && root && !root->getRoot().empty()) {
        std::vector<char> body;
        if (readFile(join(root->getRoot(), errorPage), body) == 200) response.setBody(body);
    }
    if (response.body.empty()) {
        std::ostringstream html;
        html << "<!doctype html><title>" << status << ' ' << response.statusText
             << "</title><h1>" << status << ' ' << response.statusText << "</h1>\n";
        response.setBody(html.str());
    }
    finish(client);
}

void ResponseBuilder::buildResponse(Client& client, const Config& config)
{
    HttpRequest& request = client.getRequest();
    HttpResponse& response = client.getResponse();
    response.reset();
    client.setCloseAfterResponse(!request.keepAlive());
    const LocationBlock* location = client.getServerBlock().matchLocation(request.uri);
    if (!location) { buildErrorResponse(client, 404, &config); return; }
    if (std::find(location->getAllowedMethods().begin(), location->getAllowedMethods().end(), request.method)
        == location->getAllowedMethods().end()) {
        buildErrorResponse(client, 405, &config);
        std::string allow;
        for (std::size_t i = 0; i < location->getAllowedMethods().size(); ++i) {
            if (i) allow += ", ";
            allow += location->getAllowedMethods()[i];
        }
        response.setHeader("Allow", allow); finish(client); return;
    }
    if (location->getRedirectCode()) {
        response.setStatus(location->getRedirectCode());
        response.setHeader("Location", location->getRedirectURL()); finish(client); return;
    }
    std::size_t dot = request.uri.rfind('.');
    if (request.method != "GET" || (dot != std::string::npos
        && location->getCgiExtension().count(request.uri.substr(dot)))) {
        buildErrorResponse(client, 501, &config); return;
    }
    std::string file = join(location->getRoot(), request.uri.substr(location->getPath().size()));
    struct stat info;
    bool directory = stat(file.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
    if (directory && request.uri[request.uri.size() - 1] != '/') {
        response.setStatus(301);
        response.setHeader("Location", encodePath(request.uri) + "/"
            + (request.querystring.empty() ? "" : "?" + request.querystring));
        finish(client); return;
    }
    if (directory)
        file = join(file, location->getIndex());
    std::vector<char> body;
    int status = readFile(file, body);
    if (directory && status == 404) status = location->getAutoindex() ? 501 : 403;
    if (status != 200) { buildErrorResponse(client, status, &config); return; }
    response.setBody(body); response.setHeader("Content-Type", mime(file)); finish(client);
}
