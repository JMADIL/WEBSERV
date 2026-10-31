#include "ResponseBuilder.hpp"
#include "Client.hpp"
#include "Config.hpp"
#include "ServerBlock.hpp"
#include "LocationBlock.hpp"
#include "MimeTypes.hpp"
#include "CgiHandler.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>

void ResponseBuilder::buildResponse(Client &client, const Config &config)
{
    const HttpRequest &req = client.getRequest();
    int listenPort = client.getListenPort();

    const ServerBlock *server = _matchServer(req, config, listenPort);
    if (!server)
    {
        buildErrorResponse(client, 404, &config);
        return;
    }

    const LocationBlock *loc = _matchLocation(req.uri, *server);
    if (!loc)
    {
        buildErrorResponse(client, 404, &config);
        return;
    }

    if (loc->getRedirectCode() != 0)
    {
        _handleRedirect(client, *loc);
        client.getWriteBuffer() = client.getResponse().serialize();
        client.setState(STATE_WRITING_RESPONSE);
        return;
    }

    const std::vector<std::string> &allowed = loc->getAllowedMethods();
    bool methodAllowed = false;
    for (size_t i = 0; i < allowed.size(); ++i)
    {
        if(allowed[i] == req.method)
        {
            method[i] = true;
            break;
        }
    }
    if (!methodAllowed)
    {
        std::string allowHeader;
        for (size_t i = 0; i < allowed.size(); ++i)
        {
            if (i > 0)
                allowHeader += ", ";
            allowHeader += allowed[i];
        }
        client.getResponse().setHeader("Allow", allowHeader);
        buildErrorResponse(client, 405, &config);
        return;
    }

    if (loc->getClientMaxBodySize() > 0 && req.body.size() > loc->getClientMaxBodySize())
    {
        buildErrorResponse(client, 413, &config);
        return;
    }

    if (CgiHandler::isCgi(req.uri, *loc))
    {
        _handleCGI(client, *loc, *server);
        return;
    }

    if (req.method == "GET")
        _handleGET(client, *loc, *server);
    else if (req.method == "POST")
        _handlePOST(client, *loc, *server);
    else if (req.method == "DELETE")
        _handleDELETE(client, *loc, *server);
    else
        buildErrorResponse(client, 501, &config);

    
    if (client.getState() != STATE_CGI_RUNNING)
    {
        client.getWriteBuffer() = client.getResponse().serialize();
        client.setState(STATE_WRITING_RESPONSE);
    }
}

void ResponseBuilder::buildErrorResponse(Client &client, int statusCode, const Config *config)
{
    HttpResponse &res = client.getResponse();
    res.reset();
    res.setStatus(statusCode);

    std::string errorPagePath = (config != NULL) ? config->getErrorPage(statusCode) : "";
    if (!errorPagePath.empty() && _fileExists(errorPagePath))
    {
        std::vector<char> pageContent = _readFile(errorPagePath);
        res.setBody(pageContent);
        res.setHeader("Content-Type", "text/html");
    }
    else
    {
        // Default HTML error page
        std::ostringstream oss;
        oss << "<!DOCTYPE html>\r\n"
            << "<html>\r\n"
            << "<head><title>" << statusCode << " " << res.statusText << "</title></head>\r\n"
            << "<body style=\"text-align: center; font-family: sans-serif; padding-top: 50px;\">\r\n"
            << "<h1>" << statusCode << " " << res.statusText << "</h1>\r\n"
            << "<hr style=\"width: 50%;\">\r\n"
            << "<p>webserv</p>\r\n"
            << "</body>\r\n"
            << "</html>\r\n";
        res.setBody(oss.str());
        res.setHeader("Content-Type", "text/html");
    }

    if (statusCode >= 400)
    {
        res.setHeader("Connection", "close");
    }

    client.getWriteBuffer() = res.serialize();
    client.setState(STATE_WRITING_RESPONSE);
}

const ServerBlock *ResponseBuilder::_matchServer(const HttpRequest &req, const Config &config, int listenPort)
{
    std::string host = req.getHeader("host");
    size_t colon = host.find(':');
    if (colon != std::string::npos)
        host = host.substr(0, colon);

    return config.getServerBlock(host, listenPort);
}

const LocationBlock *ResponseBuilder::_matchLocation(const std::string &uri, const ServerBlock &server)
{
    const std::vector<LocationBlock> &locations = server.getLocations();
    const LocationBlock *bestMatch = NULL;
    size_t maxLen = 0;

    for (size_t i = 0; i < locations.size(); ++i)
    {
        const std::string &locPath = locations[i].getPath();
        if (uri.find(locPath) == 0)
        {
            if(uri.length() == locPath.length() || 
            locPath[locPath.length() - 1] == '/'||
            uri[locPath.length()] == '/')
            {
                if (locPath.length() > maxLen)
                {
                    maxLen = locPath.length();
                    bestMatch = &locations[i];
                }
            }
        }
    }
    return bestMatch;
}

std::string ResponseBuilder::_resolvePath(const std::string &uri, const LocationBlock &loc)
{
    std::string root = loc.getRoot();
    std::string locPath = loc.getPath();

    std::string relPath;
    if (uri.find(locPath) == 0)
        relPath = uri.substr(locPath.length());
    else
        relPath = uri;
    
    if (!root.empty() && root[root.length() - 1] == '/')
        root = root.substr(0, root.length() - 1);
    if (!relPath.empty() && relPath[0] != '/')
        relPath = "/" + relPath;
    
    return root + relPath;
}

void ResponseBuilder::_handleGET(Client &client, const LocationBlock &loc, const ServerBlock &server)
{
    (void)server;
    const HttpRequest &req = client.getRequest();
    HttpResponse &res = client.getResponse();
    std::string path = _resolvePath(req.uri, loc);

    if (!_fileExists(path))
    {
        buildErrorResponse(client, 404);
        return;
    }

    if (_isDirectory(path))
    {
        std::string indexName = loc.getIndex();
            if (!indexName.empty())
            {
                std::string indexPath = path;
                if (indexPath[indexPath.length() - 1] != '/')
                    indexPath += "/";
                indexPath += indexName;

                if (_fileExists(indexPath) && !_isDirectory(indexPath))
                {
                    std::vector<char> content = _readFile(indexPath);
                    res.setStatus(200);
                    res.setHeader("Content-Type", MimeTypes::getType(indexPath));
                    res.setBody(content);
                    return;
                }
            }
        
        if (loc.getAutoindex())
        {
            _handleAutoindex(client, path, req.uri);
            return;
        }

        buildErrorResponse(client, 403);
            return;
    }
    std::vector<char> content = _readFile(path);
    res.setStatus(200);
    res.setHeader("Content-Type", MimeTypes::getType(path));
    res.setBody(content);
}

void ResponseBuilder::_handlePOST(Client &client, const LocationBlock &loc, const ServerBlock &server)
{
    (void)server;
    const HttpRequest &req = client.getRequest();
    HttpResponse &res = client.getResponse();

    std::string uploadDir = loc.getUploadPath();
    if (uploadDir.empty())
    {
        buildErrorResponse(client, 403);
        return;
    }

    if (!_fileExists(uploadDir) || !_isDirectory(uploadDir))
    {
        buildErrorResponse(client, 500);
        return;
    }
    
    std::string filename;
    size_t lastSlash = req.uri.rfind('/');
     if (lastSlash != std::string::npos && lastSlash + 1 < req.uri.length())
    {
        filename = req.uri.substr(lastSlash + 1);
    }
    if (filename.empty())
    {
        std::ostringstream oss;
        oss << "upload_" << static_cast<unsigned long>(std::time(NULL)) << ".bin";
        filename = oss.str();
    }

    std::string destPath = uploadDir;
    if (destPath[destPath.length() - 1] != '/')
        destPath += "/";
    destPath += filename;

    std::ofstream outfile(destPath.c_str(), std::ios::binary | std::ios::out);
    if (!outfile.is_open())
    {
        buildErrorResponse(client, 500);
        return;
    }

    if (!req.body.empty())
    {
        outfile.write(&req.body[0], req.body.size());
    }
    outfile.close();

     res.setStatus(201);
    res.setHeader("Content-Type", "text/plain");
    res.setBody("File created and uploaded successfully.\r\n");
}

void ResponseBuilder::_handleDELETE(Client &client, const LocationBlock &loc, const ServerBlock &server)
{
    (void)server;
    const HttpRequest &req = client.getRequest();
    HttpResponse &res = client.getResponse();
    std::string path = _resolvePath(req.uri, loc);

    if (!_fileExists(path))
    {
        buildErrorResponse(client, 404);
        return;
    }

    if (_isDirectory(path))
    {
        buildErrorResponse(client, 403);
        return;
    }

    if (std::remove(path.c_str()) != 0)
    {
        buildErrorResponse(client, 500);
        return;
    }

    res.setStatus(204);
    res.headers.clear();
    res.body.clear();
}

void ResponseBuilder::_handleRedirect(Client &Client, const LocationBlock &loc)
{
    HttpResponse &res = client.getResponse();
    res.setStatus(loc.getRedirectCode());
    res.setHeader("Location", loc.getRedirectURL());
    res.setHeader("Content-Type", "text/html");

     std::ostringstream oss;
    oss << "<html><body><h1>" << loc.getRedirectCode() << " Redirect</h1>\r\n"
        << "<p>The document has moved to <a href=\"" << loc.getRedirectURL() << "\">"
        << loc.getRedirectURL() << "</a>.</p></body></html>\r\n";
    res.setBody(oss.str());
}

void ResponseBuilder::_handleAutoindex(Client &client, const std::string &dirPath, const std::string &uri)
{
    DIR *dir = opendir(dirPath.c_str());
    if (!dir)
    {
        buildErrorResponse(client, 403);
        return;
    }

    std::ostringstream oss;
    std::string displayUri = uri;
    if (displayUri.empty() || displayUri[displayUri.length() - 1] != '/')
        displayUri += "/";
    
    oss << "<!DOCTYPE html>\r\n"
        << "<html>\r\n"
        << "<head><title>Index of " << displayUri << "</title></head>\r\n"
        << "<body style=\"font-family: monospace; padding: 20px;\">\r\n"
        << "<h1>Index of " << displayUri << "</h1>\r\n"
        << "<hr>\r\n"
        << "<pre>\r\n";
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        std::string name = entry->d_name;
        if(name == ".")
            continue;
        
        std::string fullItemPath = dirPath;
        if (fullItemPath[fullItemPath.length() - 1] != '/')
            fullItemPath += "/";
        fullItemPath += name; 

        struct stat st;
        std::string sizeStr = "-";
        std::string dateStr = "";

        if (stat(fullItemPath.c_str(), &st) == 0)
        {
            char timeBuf[64];
            struct tm *tmInfo = std::localtime(&st.st_mtime);
            if (tmInfo && std::strftime(timeBuf, sizeof(timeBuf), "%d-%b-%Y %H:%M", tmInfo))
                dateStr = timeBuf;

            if (S_ISDIR(st.st_mode))
            {
                name += "/";
            }
            else
            {
                std::ostringstream sizeOss;
                sizeOss << st.st_size;
                sizeStr = sizeOss.str();
            }
        }
        std::string href = displayUri + name;
        oss << "<a href=\"" << href << "\">" << name << "</a>";

        if (name.length() < 40)
            oss << std::string(40 - name.length(), ' ');
        else
            oss << " ";

        oss << dateStr << "    " << sizeStr << "\r\n";
    }
    closedir(dir);

    oss << "</pre>\r\n"
        << "<hr>\r\n"
        << "</body>\r\n"
        << "</html>\r\n";

    HttpResponse &res = client.getResponse();
    res.setStatus(200);
    res.setHeader("Content-Type", "text/html");
    res.setBody(oss.str());
}

bool ResponseBuilder::_fileExists(const std::string &path)
{
    struct stat st;
    return (stat(path.c_str(), &st) == 0);
}

bool ResponseBuilder::_isDirectory(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

std::vector<char> ResponseBuilder::_readFile(const std::string &path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open())
        return std::vector<char>();
    
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size <= 0)
        return std::vector<char>();

    std::vector<char> buffer(size);
    if (file.read(&buffer[0], size))
        return buffer;
    
    return std::vector<char>();
}