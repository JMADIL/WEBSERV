#include "CgiHandler.hpp"
#include "Client.hpp"
#include "LocationBlock.hpp"
#include <stdexcept>

namespace {
std::string normalizedPath(const std::string& uri)
{
    if (uri.empty() || uri[0] != '/')
        throw std::invalid_argument("CGI expects a decoded absolute URL path");
    for (std::size_t i = 0; i < uri.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(uri[i]);
        if (c < 32 || c == 127 || c == '\\' || c == '?' || c == '#')
            throw std::invalid_argument("unsafe CGI URL path");
    }
    std::string path = "/";
    std::size_t start = 1;
    while (start <= uri.size()) {
        std::size_t end = uri.find('/', start);
        std::string part = uri.substr(start, end - start);
        if (part == "..") throw std::invalid_argument("CGI path traversal");
        if (!part.empty() && part != ".") {
            if (path.size() > 1) path += '/';
            path += part;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (uri.size() > 1 && uri[uri.size() - 1] == '/' && path.size() > 1) path += '/';
    return path;
}
}

bool CgiHandler::resolve(const std::string& uri, const LocationBlock& location, Target& target)
{
    target = Target();
    const std::string path = normalizedPath(uri);
    const std::string& prefix = location.getPath();
    if (prefix.empty() || path.compare(0, prefix.size(), prefix) != 0
        || (path.size() > prefix.size() && prefix[prefix.size() - 1] != '/'
            && path[prefix.size()] != '/')) return false;

    const std::map<std::string, std::string>& extensions = location.getCgiExtension();
    std::size_t start = prefix.size();
    while (start < path.size()) {
        if (path[start] == '/') { ++start; continue; }
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string filename = path.substr(start, end - start);
        std::size_t dot = filename.rfind('.');
        if (dot != std::string::npos) {
            std::map<std::string, std::string>::const_iterator extension = extensions.find(filename.substr(dot));
            if (extension != extensions.end()) {
                if (location.getRoot().empty() || extension->second.empty() || extension->second[0] != '/')
                    throw std::invalid_argument("CGI requires a root and absolute interpreter path");
                Target found;
                found.scriptName = path.substr(0, end);
                found.pathInfo = path.substr(end);
                found.interpreter = extension->second;
                std::string relative = found.scriptName.substr(prefix.size());
                if (!relative.empty() && relative[0] == '/') relative.erase(0, 1);
                found.scriptPath = location.getRoot();
                if (found.scriptPath[found.scriptPath.size() - 1] != '/') found.scriptPath += '/';
                found.scriptPath += relative;
                target = found;
                return true;
            }
        }
        start = end;
    }
    return false;
}

bool CgiHandler::isCgi(const std::string& uri, const LocationBlock& location)
{
    Target target;
    return resolve(uri, location, target);
}

void CgiHandler::handle(Client& client, const LocationBlock& location)
{
    if (client.getState() != STATE_BUILDING_RESPONSE || client.getSentOffset() != 0)
        throw std::logic_error("CGI routing requires an unsent response under construction");
    Target target;
    int status;
    try { status = resolve(client.getRequest().uri, location, target) ? 501 : 404; }
    catch (const std::invalid_argument&) { status = 400; }
    // shortcut: routing only; launch/attachCgi and output parsing are steps 2 and 3.
    HttpResponse& response = client.getResponse();
    response.reset();
    response.setStatus(status);
    response.setHeader("Content-Type", "text/plain");
    response.setBody(status == 501 ? "CGI execution is not implemented yet.\n" : "Invalid CGI route.\n");
    client.setCloseAfterResponse(!client.getRequest().keepAlive());
    response.setHeader("Connection", client.getCloseAfterResponse() ? "close" : "keep-alive");
    client.getWriteBuffer() = response.serialize();
    client.setState(STATE_WRITING_RESPONSE);
}
