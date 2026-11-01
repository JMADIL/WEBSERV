#include "ResponseBuilder.hpp"
#include "CgiHandler.hpp"
#include "CgiResponse.hpp"
#include "Client.hpp"
#include <algorithm>
#include <new>
#include <sys/stat.h>

namespace {
std::string escapedPath(const std::string& path)
{
    const char* hex = "0123456789ABCDEF";
    std::string result;
    for (std::size_t i = 0; i < path.size(); ++i) {
        unsigned char c = path[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') result += c;
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
}

bool CgiHandler::handleIndex(Client& client, const LocationBlock& location)
{
    HttpRequest& request = client.getRequest();
    if (request.method != "GET" || location.getRedirectCode()
        || std::find(location.getAllowedMethods().begin(), location.getAllowedMethods().end(), "GET")
            == location.getAllowedMethods().end()) return false;
    const std::string& index = location.getIndex();
    std::size_t dot = index.rfind('.');
    if (dot == std::string::npos || !location.getCgiExtension().count(index.substr(dot))) return false;
    std::string directory = location.getRoot();
    if (directory.empty()) return false;
    if (directory[directory.size() - 1] != '/') directory += '/';
    std::string relative = request.uri.substr(location.getPath().size());
    if (!relative.empty() && relative[0] == '/') relative.erase(0, 1);
    directory += relative;
    struct stat info;
    if (stat(directory.c_str(), &info) < 0 || !S_ISDIR(info.st_mode)) return false;
    if (directory[directory.size() - 1] != '/') directory += '/';
    if (stat((directory + index).c_str(), &info) < 0) return false;
    if (request.uri[request.uri.size() - 1] != '/') {
        client.getResponse().setStatus(301);
        std::string target = escapedPath(request.uri) + '/';
        if (!request.querystring.empty()) target += '?' + request.querystring;
        client.getResponse().setHeader("Location", target);
        client.getWriteBuffer() = client.getResponse().serialize();
        client.setState(STATE_WRITING_RESPONSE);
    } else {
        request.uri += index;
        handle(client, location);
    }
    return true;
}

void CgiHandler::handle(Client& client, const LocationBlock& location)
{
    if (client.getState() != STATE_BUILDING_RESPONSE || client.getSentOffset() != 0)
        throw std::logic_error("CGI routing requires an unsent response under construction");
    // Stateless process-lifetime callbacks remain valid through cancellation and shutdown.
    static CgiResponse callbacks;
    client.setCloseAfterResponse(!client.getRequest().keepAlive());
    try {
        Target target;
        if (!resolve(client.getRequest().uri, location, target)) {
            callbacks.onCgiError(client, 404);
            return;
        }
        launch(client, location, callbacks);
    } catch (const LaunchError& error) { callbacks.onCgiError(client, error.status()); }
    catch (const std::invalid_argument&) { callbacks.onCgiError(client, 400); }
    catch (const std::bad_alloc&) { callbacks.onCgiError(client, 503); }
    catch (const std::exception&) { callbacks.onCgiError(client, 500); }
}

// Define the existing HTTP hook here so the imported HTTP source stays unchanged.
void ResponseBuilder::_handleCGI(Client& client, const LocationBlock& location, const ServerBlock& server)
{
    (void)server;
    CgiHandler::handle(client, location);
}
