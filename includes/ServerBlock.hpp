#ifndef SERVERBLOCK_HPP
# define SERVERBLOCK_HPP

# include "LocationBlock.hpp"
# include <string>
# include <vector>
# include <map>
# include <cstddef>

class ServerBlock
{
private:
    std::string                     _host;
    int                             _port;
    std::string                     _serverName;
    std::size_t                     _clientMaxBodySize;
    std::map<int, std::string>       _errorPages;
    std::vector<LocationBlock>      _locations;

public:
    ServerBlock();

    const std::string& getHost() const;
    int getPort() const;
    const std::string& getServerName() const;
    std::size_t getClientMaxBodySize() const;
    std::string getErrorPage(int statusCode) const;
    const std::vector<LocationBlock>& getLocations() const;

    void setHost(const std::string& host);
    void setPort(int port);
    void setServerName(const std::string& name);
    void setClientMaxBodySize(std::size_t bytes);
    void setErrorPage(int statusCode, const std::string& path);
    void addLocation(const LocationBlock& location);
    const LocationBlock* matchLocation(const std::string& uriPath) const;
    void applyLocationDefaults();
};

#endif