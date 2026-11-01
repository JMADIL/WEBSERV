#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <limits>
#include <set>
#include <sys/stat.h>

namespace
{
struct Token
{
    std::string text;
    std::size_t line;

    Token(const std::string& value, std::size_t number)
        : text(value), line(number) {}
};

void fail(const std::string& file, std::size_t line,
          const std::string& message)
{
    std::ostringstream out;
    out << file << ':' << line << ": " << message;
    throw std::runtime_error(out.str());
}

bool punctuation(char c)
{
    return c == '{' || c == '}' || c == ';';
}

std::vector<Token> tokenize(const std::string& file)
{
    struct stat info;
    if (stat(file.c_str(), &info) != 0)
        fail(file, 1, "cannot open configuration file");
    if (!S_ISREG(info.st_mode))
        fail(file, 1, "configuration must be a regular file");
    std::ifstream input(file.c_str(), std::ios::in | std::ios::binary);
    if (!input)
        fail(file, 1, "cannot open configuration file");
    std::vector<Token> tokens;
    std::string word;
    std::size_t line = 1;
    std::size_t wordLine = 1;
    std::size_t bytes = 0;
    bool comment = false;
    char c;
    while (input.get(c))
    {
        if (++bytes > 1024u * 1024u)
            fail(file, line, "configuration exceeds 1 MiB");
        if (comment && c != '\n')
            continue;
        comment = false;
        bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        if (space || punctuation(c) || c == '#')
        {
            if (!word.empty())
            {
                tokens.push_back(Token(word, wordLine));
                word.clear();
            }
            if (punctuation(c))
                tokens.push_back(Token(std::string(1, c), line));
            if (c == '#')
                comment = true;
            if (c == '\n')
                ++line;
        }
        else
        {
            if (c == '\'' || c == '"' || c == '\\')
                fail(file, line, "quoted values and escapes are not supported");
            if (static_cast<unsigned char>(c) < 32
                || static_cast<unsigned char>(c) == 127)
                fail(file, line, "invalid control character");
            if (word.empty())
                wordLine = line;
            word += c;
        }
    }
    if (input.bad() || !input.eof())
        fail(file, line, "could not read configuration file");
    if (!word.empty())
        tokens.push_back(Token(word, wordLine));
    tokens.push_back(Token("", line));
    return tokens;
}

class Parser
{
public:
    Parser(const std::string& file, const std::vector<Token>& tokens)
        : _file(file), _tokens(tokens), _pos(0) {}

    std::vector<ServerBlock> parse()
    {
        std::vector<ServerBlock> servers;
        while (!peek().text.empty())
        {
            Token start = peek();
            expect("server");
            ServerBlock server = parseServer();
            for (std::size_t i = 0; i < servers.size(); ++i)
            {
                if (servers[i].getPort() == server.getPort()
                    && (servers[i].getHost() == server.getHost()
                        || servers[i].getHost() == "0.0.0.0"
                        || server.getHost() == "0.0.0.0"))
                    error(start, "duplicate or overlapping listening endpoint");
            }
            servers.push_back(server);
        }
        if (servers.empty())
            error(peek(), "expected at least one server block");
        return servers;
    }

private:
    const std::string& _file;
    const std::vector<Token>& _tokens;
    std::size_t _pos;

    const Token& peek() const { return _tokens[_pos]; }

    void error(const Token& token, const std::string& message) const
    {
        fail(_file, token.line, message);
    }

    void expect(const std::string& value)
    {
        if (peek().text != value)
            error(peek(), "expected '" + value + "', got '" + peek().text + "'");
        ++_pos;
    }

    std::vector<Token> arguments()
    {
        std::vector<Token> args;
        while (peek().text != ";")
        {
            if (peek().text.empty() || peek().text == "{" || peek().text == "}")
                error(peek(), "expected arguments followed by ';'");
            args.push_back(peek());
            ++_pos;
        }
        ++_pos;
        return args;
    }

    void count(const Token& name, const std::vector<Token>& args,
               std::size_t expected) const
    {
        if (args.size() != expected)
            error(name, "wrong number of arguments for '" + name.text + "'");
    }

    void unique(std::set<std::string>& seen, const Token& name) const
    {
        if (!seen.insert(name.text).second)
            error(name, "duplicate directive '" + name.text + "'");
    }

    std::size_t number(const Token& token, std::size_t limit) const
    {
        if (token.text.empty())
            error(token, "expected decimal digits");
        std::size_t value = 0;
        for (std::size_t i = 0; i < token.text.size(); ++i)
        {
            char c = token.text[i];
            if (c < '0' || c > '9')
                error(token, "expected decimal digits: '" + token.text + "'");
            std::size_t digit = static_cast<std::size_t>(c - '0');
            if (digit > limit || value > (limit - digit) / 10)
                error(token, "numeric value out of range: '" + token.text + "'");
            value = value * 10 + digit;
        }
        return value;
    }

    std::size_t bodySize(const Token& token) const
    {
        Token digits = token;
        std::size_t multiplier = 1;
        char suffix = digits.text[digits.text.size() - 1];
        if (suffix == 'K' || suffix == 'M')
        {
            multiplier = suffix == 'K' ? 1024u : 1024u * 1024u;
            digits.text.erase(digits.text.size() - 1);
        }
        return number(digits, std::numeric_limits<std::size_t>::max() / multiplier)
            * multiplier;
    }

    void uriPath(const Token& token) const
    {
        const std::string& path = token.text;
        if (path.empty() || path[0] != '/'
            || path.find_first_of("?%") != std::string::npos
            || path.find("//") != std::string::npos)
            error(token, "expected a literal normalized URL path beginning with '/'");
        std::size_t start = 1;
        while (start <= path.size())
        {
            std::size_t end = path.find('/', start);
            std::string segment = path.substr(start, end - start);
            if (segment == "." || segment == "..")
                error(token, "dot segments are not allowed in URL paths");
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }

    void listen(ServerBlock& server, const Token& token) const
    {
        std::string host = "0.0.0.0";
        std::string port = token.text;
        std::size_t colon = port.find(':');
        if (colon != std::string::npos)
        {
            host = port.substr(0, colon);
            port = port.substr(colon + 1);
            std::ostringstream normalized;
            std::size_t start = 0;
            for (int part = 0; part < 4; ++part)
            {
                std::size_t end = host.find('.', start);
                if ((part < 3 && end == std::string::npos)
                    || (part == 3 && end != std::string::npos))
                    error(token, "listen requires a numeric IPv4 address");
                Token octet(host.substr(start, end - start), token.line);
                if (part)
                    normalized << '.';
                normalized << number(octet, 255);
                if (end != std::string::npos)
                    start = end + 1;
            }
            host = normalized.str();
        }
        std::size_t value = number(Token(port, token.line), 65535);
        if (value == 0)
            error(token, "listen port must be between 1 and 65535");
        server.setHost(host);
        server.setPort(static_cast<int>(value));
    }

    LocationBlock parseLocation()
    {
        Token path = peek();
        uriPath(path);
        ++_pos;
        expect("{");
        LocationBlock location;
        location.setPath(path.text);
        std::set<std::string> seen;
        std::set<std::string> extensions;
        while (peek().text != "}")
        {
            Token name = peek();
            if (name.text.empty())
                error(name, "missing '}' for location block");
            ++_pos;
            std::vector<Token> args = arguments();
            if (name.text != "cgi_extension")
                unique(seen, name);
            if (name.text == "allowed_methods")
            {
                if (args.empty())
                    error(name, "allowed_methods requires at least one method");
                std::set<std::string> methods;
                std::vector<std::string> values;
                for (std::size_t i = 0; i < args.size(); ++i)
                {
                    const std::string& method = args[i].text;
                    if ((method != "GET" && method != "POST" && method != "DELETE")
                        || !methods.insert(method).second)
                        error(args[i], "unsupported or duplicate HTTP method");
                    values.push_back(method);
                }
                location.setAllowedMethods(values);
            }
            else if (name.text == "redirect")
            {
                count(name, args, 2);
                std::size_t code = number(args[0], 999);
                if (code != 301 && code != 302)
                    error(args[0], "redirect supports only 301 and 302");
                if (args[1].text[0] != '/' && args[1].text.compare(0, 7, "http://") != 0
                    && args[1].text.compare(0, 8, "https://") != 0)
                    error(args[1], "redirect requires an absolute URL or a path beginning with '/'");
                if (args[1].text[0] != '/') {
                    std::size_t host = args[1].text.find("://") + 3;
                    if (host == args[1].text.size() || args[1].text[host] == '/'
                        || args[1].text[host] == '?' || args[1].text[host] == ':')
                        error(args[1], "redirect URL requires a host");
                }
                location.setRedirect(static_cast<int>(code), args[1].text);
            }
            else if (name.text == "cgi_extension")
            {
                count(name, args, 2);
                const std::string& ext = args[0].text;
                if (ext.size() < 2 || ext[0] != '.'
                    || ext.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-", 1) != std::string::npos
                    || args[1].text[0] != '/')
                    error(name, "cgi_extension requires .extension and an absolute interpreter path");
                if (!extensions.insert(ext).second)
                    error(args[0], "duplicate CGI extension");
                location.setCgiExtension(ext, args[1].text);
            }
            else if (name.text == "root" || name.text == "index"
                     || name.text == "upload_path" || name.text == "autoindex"
                     || name.text == "client_max_body_size")
            {
                count(name, args, 1);
                const std::string& value = args[0].text;
                if (name.text == "root")
                    location.setRoot(value);
                else if (name.text == "index")
                {
                    if (value == "." || value == ".." || value.find_first_of("/?%") != std::string::npos)
                        error(args[0], "index must be a single filename");
                    location.setIndex(value);
                }
                else if (name.text == "upload_path")
                    location.setUploadPath(value);
                else if (name.text == "autoindex")
                {
                    if (value != "on" && value != "off")
                        error(args[0], "autoindex must be 'on' or 'off'");
                    location.setAutoindex(value == "on");
                }
                else
                    location.setClientMaxBodySize(bodySize(args[0]));
            }
            else
                error(name, "unknown or misplaced location directive '" + name.text + "'");
        }
        ++_pos;
        if (location.getRoot().empty() && location.getRedirectCode() == 0)
            error(path, "file-serving location requires a root directory");
        return location;
    }

    ServerBlock parseServer()
    {
        expect("{");
        ServerBlock server;
        std::set<std::string> seen;
        std::set<std::string> paths;
        std::set<int> errorCodes;
        while (peek().text != "}")
        {
            Token name = peek();
            if (name.text.empty())
                error(name, "missing '}' for server block");
            ++_pos;
            if (name.text == "location")
            {
                LocationBlock location = parseLocation();
                if (!paths.insert(location.getPath()).second)
                    error(name, "duplicate location path");
                server.addLocation(location);
                continue;
            }
            std::vector<Token> args = arguments();
            if (name.text == "error_page")
            {
                count(name, args, 2);
                int code = static_cast<int>(number(args[0], 599));
                if (code < 400)
                    error(args[0], "error_page requires a status between 400 and 599");
                uriPath(args[1]);
                if (!errorCodes.insert(code).second)
                    error(args[0], "duplicate error-page status");
                server.setErrorPage(code, args[1].text);
            }
            else
            {
                unique(seen, name);
                count(name, args, 1);
                if (name.text == "listen")
                    listen(server, args[0]);
                else if (name.text == "server_name")
                    server.setServerName(args[0].text);
                else if (name.text == "client_max_body_size")
                    server.setClientMaxBodySize(bodySize(args[0]));
                else
                    error(name, "unknown or misplaced server directive '" + name.text + "'");
            }
        }
        Token end = peek();
        ++_pos;
        if (seen.find("listen") == seen.end())
            error(end, "server requires a listen directive");
        if (server.getLocations().empty())
            error(end, "server requires at least one location");
        server.applyLocationDefaults();
        return server;
    }
};
}

Config::Config() : _serversView(NULL), _selectedServer(NULL) {}

Config::Config(const Config& other)
    : _servers(other._servers), _serversView(other._serversView), _selectedServer(other._selectedServer) {}

Config::Config(const std::vector<ServerBlock>& servers, const ServerBlock& selected)
    : _serversView(&servers), _selectedServer(&selected) {}

Config& Config::operator=(const Config& other)
{
    if (this == &other) return *this;
    if (_serversView || other._serversView)
        throw std::logic_error("request Config views cannot be assigned");
    std::vector<ServerBlock> servers(other._servers);
    _servers.swap(servers);
    return *this;
}

void Config::load(const std::string& filePath)
{
    if (_serversView) throw std::logic_error("request Config views are read-only");
    std::vector<Token> tokens = tokenize(filePath);
    Parser parser(filePath, tokens);
    std::vector<ServerBlock> servers = parser.parse();
    // Keep the previous usable configuration if any read/parse step fails.
    _servers.swap(servers);
}

const std::vector<ServerBlock>& Config::getServers() const
{
    return _serversView ? *_serversView : _servers;
}

Config Config::forServer(const ServerBlock& server) const
{
    const std::vector<ServerBlock>& servers = getServers();
    for (std::size_t i = 0; i < servers.size(); ++i)
        if (&servers[i] == &server) return Config(servers, server);
    throw std::invalid_argument("selected server does not belong to this Config");
}

const ServerBlock* Config::getServerBlock(const std::string& host, int listenPort) const
{
    (void)host; // Name-based virtual hosts are outside this project's current configuration model.
    if (_selectedServer)
        return _selectedServer->getPort() == listenPort ? _selectedServer : NULL;
    const ServerBlock* match = NULL;
    const std::vector<ServerBlock>& servers = getServers();
    for (std::size_t i = 0; i < servers.size(); ++i) {
        if (servers[i].getPort() != listenPort) continue;
        if (match) return NULL; // A port alone cannot distinguish two local interfaces.
        match = &servers[i];
    }
    return match;
}

std::string Config::getErrorPage(int statusCode) const
{
    const std::vector<ServerBlock>& servers = getServers();
    const ServerBlock* server = _selectedServer;
    if (!server && servers.size() == 1) server = &servers[0];
    if (!server) return "";
    return resolveErrorPage(*server, statusCode);
}

std::string Config::resolveErrorPage(const ServerBlock& server, int statusCode)
{
    std::string uri = server.getErrorPage(statusCode);
    if (uri.empty()) return "";
    const LocationBlock* location = server.matchLocation(uri);
    if (!location || location->getRoot().empty() || location->getRedirectCode()) return "";
    std::string relative = uri.substr(location->getPath().size());
    if (!relative.empty() && relative[0] == '/') relative.erase(0, 1);
    // The imported error renderer reads a static file; do not expose a CGI script.
    std::size_t start = 0;
    while (start < relative.size()) {
        std::size_t end = relative.find('/', start);
        std::string segment = relative.substr(start, end - start);
        std::size_t dot = segment.rfind('.');
        if (dot != std::string::npos && location->getCgiExtension().count(segment.substr(dot))) return "";
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::string path = location->getRoot();
    if (!relative.empty()) {
        if (path[path.size() - 1] != '/') path += '/';
        path += relative;
    }
    return path;
}
