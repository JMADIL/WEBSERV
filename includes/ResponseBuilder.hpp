#ifndef RESPONSEBUILDER_HPP
#define RESPONSEBUILDER_HPP

# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "CgiHandler.hpp"
# include "MimeTypes.hpp"

class Client;
class Config;
class ServerBlock;
class LocationBlock;

class ResponseBuilder
{
    public:
        static void buildResponse(Client &client, const Config &config);

        static void buildErrorResponse(Client &client, int statusCode,  const Config *config = NULL);
    private:
        static const ServerBlock *_matchServer(const HttpRequest &req, const Config &config, int listenPort);

        static const LocationBlock* _matchLocation(const std::string &uri, const ServerBlock &server);

        //methods
        static void _handleGET(Client &client, const LocationBlock &loc, const ServerBlock &server);

        static void _handlePOST(Client &client, const LocationBlock &loc, const ServerBlock &server);

        static void _handleDELETE(Client &client, const LocationBlock &loc, const ServerBlock &server);

        //Special handlers
        static void _handleRedirect(Client &client, const LocationBlock &loc);

        static void _handleAutoindex(Client &client, const std::string &dirPath, const std::string &uri);
        
        static void _handleCGI(Client &client, const LocationBlock &loc, const ServerBlock &server);
        
        //File utilities
        static std::string          _resolvePath(const std::string &uri, const LocationBlock &loc);

        static bool                 _fileExists(const std::string &path);
        
        static bool                 _isDirectory(const std::string &path);
        
        static std::vector<char>    _readFile(const std::string &path);
};

#endif
