#ifndef CGIHANDLER_HPP
# define CGIHANDLER_HPP
# include <string>
# include <stdexcept>
# include <sys/types.h>

class Client;
class LocationBlock;
class Server;
class CgiCallbacks;

class CgiHandler
{
public:
    class LaunchError : public std::runtime_error {
        int _status;
    public:
        LaunchError(int status, const std::string& message)
            : std::runtime_error(message), _status(status) {}
        int status() const { return _status; }
    };
    struct Target {
        std::string scriptName;
        std::string scriptPath;
        std::string pathInfo;
        std::string interpreter;
    };

    // Input is the decoded URL path; the HTTP parser stores query text separately.
    static bool resolve(const std::string& uri, const LocationBlock& location, Target& target);
    static bool isCgi(const std::string& uri, const LocationBlock& location);
    static void handle(Client& client, const LocationBlock& location);
    // Intercept CGI directory indexes before the static HTTP handler reads them.
    static bool handleIndex(Client& client, const LocationBlock& location);
    // Callback lifetime must cover the job; ownership transfers only after attachCgi succeeds.
    static void launch(Client& client, const LocationBlock& location, CgiCallbacks& callbacks);

private:
    friend class Server;
    struct Process { pid_t pid; int inputFd; int outputFd; };
    static Process spawn(Client& client, const Target& target, const Server& runtime);
};
#endif
