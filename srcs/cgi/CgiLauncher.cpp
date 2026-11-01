#include "CgiHandler.hpp"
#include "Server.hpp"
#include "CgiCallbacks.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace {
struct PipeEnds {
    int input[2];
    int output[2];
    PipeEnds() { input[0] = input[1] = output[0] = output[1] = -1; }
    ~PipeEnds() {
        if (input[0] >= 0) close(input[0]);
        if (input[1] >= 0) close(input[1]);
        if (output[0] >= 0) close(output[0]);
        if (output[1] >= 0) close(output[1]);
    }
private:
    PipeEnds(const PipeEnds&);
    PipeEnds& operator=(const PipeEnds&);
};

// Missing standard descriptors must not make dup2 clobber another pipe end.
void aboveStdio(int& fd)
{
    if (fd > 2) return;
    int copies[3], count = 0, current = fd;
    while (current <= 2) {
        int next = dup(current);
        if (next < 0) {
            for (int i = 0; i < count; ++i) close(copies[i]);
            throw CgiHandler::LaunchError(503, "cannot reserve CGI descriptors");
        }
        copies[count++] = next;
        current = next;
    }
    close(fd);
    for (int i = 0; i + 1 < count; ++i) close(copies[i]);
    fd = current;
}

void verifyScript(const std::string& path)
{
    std::size_t start = path[0] == '/' ? 1 : 0;
    while (true) {
        std::size_t end = path.find('/', start);
        std::string prefix = path.substr(0, end);
        if (!prefix.empty()) {
            int flags = O_RDONLY | O_NONBLOCK | O_NOFOLLOW;
            if (end != std::string::npos) flags |= O_DIRECTORY;
            int fd = open(prefix.c_str(), flags);
            if (fd < 0) {
                int status = errno == EMFILE || errno == ENFILE ? 503
                    : errno == ENOENT || errno == ENOTDIR ? 404 : 403;
                throw CgiHandler::LaunchError(status, "CGI script is missing or inaccessible");
            }
            close(fd);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    struct stat info;
    if (stat(path.c_str(), &info) < 0 || !S_ISREG(info.st_mode))
        throw CgiHandler::LaunchError(403, "CGI script must be a regular file");
    // Configured roots must not be renamed concurrently by an untrusted writer.
}

std::string decimal(std::size_t value)
{ std::ostringstream out; out << value; return out.str(); }

void variable(std::vector<std::string>& environment, const std::string& key, const std::string& value)
{
    if (value.find('\0') != std::string::npos)
        throw CgiHandler::LaunchError(400, "NUL in CGI environment value");
    environment.push_back(key + "=" + value);
}

std::string headerVariable(const std::string& header)
{
    std::string result = "HTTP_";
    for (std::size_t i = 0; i < header.size(); ++i) {
        char c = header[i];
        if (c >= 'a' && c <= 'z') c += 'A' - 'a';
        if (c == '-') c = '_';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            throw CgiHandler::LaunchError(400, "invalid CGI request header name");
        result += c;
    }
    return result;
}

void environmentFor(Client& client, const CgiHandler::Target& target,
                    const std::string& filename, std::vector<std::string>& environment)
{
    const HttpRequest& request = client.getRequest();
    sockaddr_in local;
    socklen_t length = sizeof(local);
    if (getsockname(client.getFd(), reinterpret_cast<sockaddr*>(&local), &length) < 0
        || local.sin_family != AF_INET)
        throw CgiHandler::LaunchError(500, "cannot inspect CGI client endpoint");
    unsigned long ip = ntohl(local.sin_addr.s_addr);
    std::ostringstream address;
    address << ((ip >> 24) & 255) << '.' << ((ip >> 16) & 255)
            << '.' << ((ip >> 8) & 255) << '.' << (ip & 255);
    std::string serverName = client.getServerBlock().getServerName();
    if (serverName.empty()) {
        serverName = request.getHeader("host");
        serverName = serverName.substr(0, serverName.find(':'));
        if (serverName.empty()) serverName = address.str();
    }
    variable(environment, "GATEWAY_INTERFACE", "CGI/1.1");
    variable(environment, "SERVER_SOFTWARE", "webserv");
    variable(environment, "SERVER_PROTOCOL", request.version);
    variable(environment, "SERVER_NAME", serverName);
    variable(environment, "SERVER_ADDR", address.str());
    variable(environment, "SERVER_PORT", decimal(client.getListenPort()));
    variable(environment, "REQUEST_METHOD", request.method);
    variable(environment, "QUERY_STRING", request.querystring);
    variable(environment, "SCRIPT_NAME", target.scriptName);
    // Relative configurations execute from the script directory, so basename remains valid.
    variable(environment, "SCRIPT_FILENAME", target.scriptPath[0] == '/' ? target.scriptPath : filename);
    variable(environment, "PATH_INFO", target.pathInfo);
    variable(environment, "CONTENT_LENGTH", decimal(request.body.size()));
    if (request.hasHeader("content-type"))
        variable(environment, "CONTENT_TYPE", request.getHeader("content-type"));
    if (!client.getRemoteAddress().empty()) {
        variable(environment, "REMOTE_ADDR", client.getRemoteAddress());
        variable(environment, "REMOTE_PORT", decimal(client.getRemotePort()));
    }
    variable(environment, "REDIRECT_STATUS", "200"); // php-cgi with force_redirect enabled.
    variable(environment, "PATH", "/usr/bin:/bin");
    std::map<std::string, bool> exported;
    for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
         it != request.headers.end(); ++it) {
        std::string name = headerVariable(it->first);
        // The body is decoded already; Proxy must not become the HTTP_PROXY process setting.
        if (name == "HTTP_CONTENT_LENGTH" || name == "HTTP_CONTENT_TYPE"
            || name == "HTTP_TRANSFER_ENCODING" || name == "HTTP_PROXY") continue;
        if (!exported.insert(std::make_pair(name, true)).second)
            throw CgiHandler::LaunchError(400, "ambiguous CGI header environment name");
        variable(environment, name, it->second);
    }
}

void childFailure()
{
    close(0); close(1); close(2);
    std::exit(127);
}
}

CgiHandler::Process CgiHandler::spawn(Client& client, const Target& target, const Server& runtime)
{
    verifyScript(target.scriptPath);
    struct stat interpreter;
    if (stat(target.interpreter.c_str(), &interpreter) < 0 || !S_ISREG(interpreter.st_mode)
        || access(target.interpreter.c_str(), X_OK) < 0)
        throw LaunchError(502, "CGI interpreter is missing or not executable");
    std::size_t slash = target.scriptPath.rfind('/');
    std::string directory = slash == 0 ? "/" : target.scriptPath.substr(0, slash);
    std::string filename = target.scriptPath.substr(slash + 1);
    std::vector<std::string> environment;
    environmentFor(client, target, filename, environment);
    std::vector<char*> envp;
    envp.reserve(environment.size() + 1);
    for (std::size_t i = 0; i < environment.size(); ++i)
        envp.push_back(const_cast<char*>(environment[i].c_str()));
    envp.push_back(NULL);
    char* argv[3] = {const_cast<char*>(target.interpreter.c_str()),
                     const_cast<char*>(filename.c_str()), NULL};

    PipeEnds pipes;
    if (pipe(pipes.input) < 0 || pipe(pipes.output) < 0)
        throw LaunchError(503, "cannot create CGI pipes");
    aboveStdio(pipes.input[0]); aboveStdio(pipes.input[1]);
    aboveStdio(pipes.output[0]); aboveStdio(pipes.output[1]);
    if (fcntl(pipes.input[1], F_SETFL, O_NONBLOCK) < 0
        || fcntl(pipes.output[0], F_SETFL, O_NONBLOCK) < 0)
        throw LaunchError(503, "cannot make CGI parent pipes non-blocking");
    pid_t pid = fork();
    if (pid < 0) throw LaunchError(503, "cannot fork CGI child");
    if (pid == 0) {
        if (dup2(pipes.input[0], 0) < 0 || dup2(pipes.output[1], 1) < 0) childFailure();
        close(pipes.input[0]); close(pipes.input[1]);
        close(pipes.output[0]); close(pipes.output[1]);
        runtime.closeCgiDescriptorsInChild();
        if (std::signal(SIGINT, SIG_DFL) == SIG_ERR || std::signal(SIGTERM, SIG_DFL) == SIG_ERR
            || std::signal(SIGPIPE, SIG_DFL) == SIG_ERR || chdir(directory.c_str()) < 0)
            childFailure();
        execve(target.interpreter.c_str(), argv, &envp[0]);
        childFailure();
    }
    Process process = {pid, pipes.input[1], pipes.output[0]};
    pipes.input[1] = pipes.output[0] = -1;
    return process;
}

void CgiHandler::launch(Client& client, const LocationBlock& location, CgiCallbacks& callbacks)
{
    Server* runtime = client.getRuntimeServer();
    if (!runtime || runtime->getClient(client.getFd()) != &client)
        throw LaunchError(500, "CGI client has no owning runtime");
    if (std::find(location.getAllowedMethods().begin(), location.getAllowedMethods().end(),
                  client.getRequest().method) == location.getAllowedMethods().end())
        throw LaunchError(405, "method is not allowed for CGI");
    if (client.getRequest().body.size() > location.getClientMaxBodySize())
        throw LaunchError(413, "CGI request body exceeds the route limit");
    Target target;
    try {
        if (!resolve(client.getRequest().uri, location, target)) throw LaunchError(404, "not a CGI route");
    } catch (const std::invalid_argument&) { throw LaunchError(400, "invalid CGI route"); }
    runtime->launchCgi(client.getFd(), target, callbacks);
}

void Server::launchCgi(int clientFd, const CgiHandler::Target& target, CgiCallbacks& callbacks)
{
    Client* client = getClient(clientFd);
    if (_insideCgiCallback || !client || client->getState() != STATE_BUILDING_RESPONSE
        || client->getSentOffset() != 0)
        throw CgiHandler::LaunchError(500, "invalid CGI launch state");
    if (_cgiJobs.size() + _retiredJobs.size() >= _limits.maxCgiJobs || overBudget())
        throw CgiHandler::LaunchError(503, "CGI admission limit reached");
    // Reserve cleanup storage before fork so a failed handoff cannot leak a child.
    std::list<CgiJob> pending(1);
    CgiHandler::Process process = CgiHandler::spawn(*client, target, *this);
    try {
        attachCgi(clientFd, process.pid, process.inputFd, process.outputFd,
                  callbacks, 15, _limits.maxCgiOutput);
    } catch (...) {
        close(process.inputFd); close(process.outputFd);
        pending.front().pid = process.pid;
        _retiredJobs.splice(_retiredJobs.end(), pending);
        kill(process.pid, SIGKILL);
        throw;
    }
}

void Server::closeCgiDescriptorsInChild() const
{
    for (std::size_t i = 0; i < _listeners.size(); ++i)
        if (_listeners[i].fd > 1) close(_listeners[i].fd);
    for (std::map<int, Client*>::const_iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (it->first > 1) close(it->first);
        if (it->second->getFileFd() > 1) close(it->second->getFileFd());
    }
    for (std::list<CgiJob>::const_iterator it = _cgiJobs.begin(); it != _cgiJobs.end(); ++it) {
        if (it->inputFd > 1) close(it->inputFd);
        if (it->outputFd > 1) close(it->outputFd);
    }
    for (std::list<CgiJob>::const_iterator it = _retiredJobs.begin(); it != _retiredJobs.end(); ++it) {
        if (it->inputFd > 1) close(it->inputFd);
        if (it->outputFd > 1) close(it->outputFd);
    }
}
