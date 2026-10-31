#ifndef SERVER_HPP
# define SERVER_HPP

# include "Config.hpp"
# include "Client.hpp"
# include <map>
# include <vector>
# include <poll.h>

class Server
{
public:
    enum { MAX_CLIENTS = 1024 };
    struct Listener
    {
        int fd;
        const ServerBlock* settings;
    };

    explicit Server(const Config& config);
    ~Server();
    void initializeListeners();
    const std::vector<Listener>& getListeners() const;
    Client* acceptReady(const struct pollfd& event);
    Client* getClient(int fd);
    std::size_t getClientCount() const;
    void closeClient(int fd);
    void closeAll();
    void run();

private:
    Server(const Server&);
    Server& operator=(const Server&);

    const Config& _config;
    std::vector<Listener> _listeners;
    std::map<int, Client*> _clients;
    void processInput(Client& client);
    void readClient(Client& client);
    void writeClient(Client& client);
};

#endif
