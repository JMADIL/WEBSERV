#ifndef SERVER_HPP
# define SERVER_HPP

# include "Config.hpp"
# include "Client.hpp"
# include <map>
# include <vector>
# include <poll.h>
# include <list>
# include <sys/types.h>
# include "CgiCallbacks.hpp"
# include "CoreLimits.hpp"
# include "CgiHandler.hpp"

class Server
{
public:
    enum { MAX_CLIENTS = 1024 };
    struct Listener
    {
        int fd;
        const ServerBlock* settings;
    };

    explicit Server(const Config& config, const CoreLimits& limits = CoreLimits());
    ~Server();
    void initializeListeners();
    const std::vector<Listener>& getListeners() const;
    Client* acceptReady(const struct pollfd& event);
    Client* getClient(int fd);
    std::size_t getClientCount() const;
    void closeClient(int fd);
    void closeAll();
    void run();
    std::size_t bufferedBytes() const;
    // Ownership of pid and parent pipe ends transfers only on successful return.
    void attachCgi(int clientFd, pid_t pid, int inputFd, int outputFd,
                   CgiCallbacks& callbacks, unsigned int timeoutSeconds = 15,
                   std::size_t maxOutputBytes = 8u * 1024u * 1024u);

private:
    friend class CgiHandler;
    void launchCgi(int clientFd, const CgiHandler::Target& target, CgiCallbacks& callbacks);
    void closeCgiDescriptorsInChild() const;
    Server(const Server&);
    Server& operator=(const Server&);

    const Config& _config;
    const CoreLimits _limits;
    std::vector<Listener> _listeners;
    std::map<int, Client*> _clients;
    bool _insideCgiCallback;
    bool _running;
    void processInput(Client& client);
    void readClient(Client& client);
    void writeClient(Client& client);
    struct CgiJob {
        int clientFd;
        pid_t pid;
        int inputFd;
        int outputFd;
        std::size_t inputOffset;
        std::string output;
        CgiCallbacks* callbacks;
        std::time_t started;
        unsigned int timeout;
        std::size_t limit;
        bool exited;
        int waitStatus;
        int errorStatus;
        CgiJob() : clientFd(-1), pid(-1), inputFd(-1), outputFd(-1), inputOffset(0),
            callbacks(NULL), started(0), timeout(0), limit(0), exited(false),
            waitStatus(0), errorStatus(0) {}
    };
    std::list<CgiJob> _cgiJobs;
    std::list<CgiJob> _retiredJobs;
    std::list<CgiJob>::iterator findCgi(int clientFd);
    void cancelCgi(std::list<CgiJob>::iterator job);
    void pollCgiChildren();
    void transferCgi(const pollfd& event, int clientFd, bool input);
    void finishCgiJobs();
    bool ownsFd(int fd) const;
    bool overBudget() const;
    void refuseBusy(Client& client);
};

#endif
