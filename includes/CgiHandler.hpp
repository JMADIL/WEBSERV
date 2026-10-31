#ifndef CGIHANDLER_HPP
# define CGIHANDLER_HPP

# include "HttpRequest.hpp"
# include <string>
# include <map>

class Client;
class LocationBlock;

class CgiHandler
{
    public:
        static bool isCgi(const std::string& uri, const LocationBlock& loc);

        static int  execute(Client& client, const LocationBlock& loc);

        static bool readOutput(int pipeFd, Client& client);

        static void parseCgiOutput(Client& client);

    private:
        static char**   _buildEnvp(const HttpRequest& req, const LocationBlock& loc);

        static void     _freeEnvp(char** envp);

        static std::string _getScriptPath(const std::string& uri, const LocationBlock& loc);
        
        static std::string _getInterpreter(const std::string& uri, const LocationBlock& loc);
};

#endif
