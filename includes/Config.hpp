#ifndef CONFIG_HPP
# define CONFIG_HPP

# include "ServerBlock.hpp"
# include <string>
# include <vector>

class Config
{
public:
    Config();
    Config(const Config& other);
    Config& operator=(const Config& other);
    void load(const std::string& filePath);
    const std::vector<ServerBlock>& getServers() const;
    // Borrow settings for one request; the owning Config must remain unchanged/alive.
    Config forServer(const ServerBlock& server) const;
    const ServerBlock* getServerBlock(const std::string& host, int listenPort) const;
    std::string getErrorPage(int statusCode) const;

private:
    std::vector<ServerBlock> _servers;
    const std::vector<ServerBlock>* _serversView;
    const ServerBlock* _selectedServer;
    Config(const std::vector<ServerBlock>& servers, const ServerBlock& selected);
};

#endif
