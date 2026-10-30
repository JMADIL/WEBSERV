#include "ServerBlock.hpp"

ServerBlock::ServerBlock()
    : _host("0.0.0.0"),
      _port(0),
      _clientMaxBodySize(1024 * 1024)
{
}
const std::string& ServerBlock::getHost() const
{
    return _host;
}

void ServerBlock::setHost(const std::string& host)
{
    _host = host;
}

int ServerBlock::getPort() const
{
    return _port;
}

void ServerBlock::setPort(int port)
{
    _port = port;
}

const std::string& ServerBlock::getServerName() const
{
    return _serverName;
}

void ServerBlock::setServerName(const std::string& name)
{
    _serverName = name;
}

std::size_t ServerBlock::getClientMaxBodySize() const
{
    return _clientMaxBodySize;
}

void ServerBlock::setClientMaxBodySize(std::size_t bytes)
{
    _clientMaxBodySize = bytes;
}

const std::vector<LocationBlock>&
ServerBlock::getLocations() const
{
    return _locations;
}

void ServerBlock::addLocation(const LocationBlock& location)
{
    _locations.push_back(location);
}
void ServerBlock::setErrorPage(
    int statusCode, const std::string& path)
{
    _errorPages[statusCode] = path;
}

std::string ServerBlock::getErrorPage(int statusCode) const
{
    std::map<int, std::string>::const_iterator it =
        _errorPages.find(statusCode);

    if (it != _errorPages.end())
        return it->second;

    return "";
}
const LocationBlock* ServerBlock::matchLocation(
    const std::string& uriPath) const
{
    const LocationBlock* best = NULL;
    std::size_t bestLength = 0;

    for (std::size_t i = 0; i < _locations.size(); ++i)
    {
        const std::string& path = _locations[i].getPath();

        if (path.empty() || path.size() > uriPath.size())
            continue;

        if (uriPath.compare(0, path.size(), path) != 0)
            continue;

        bool boundary =
            uriPath.size() == path.size()
            || path[path.size() - 1] == '/'
            || uriPath[path.size()] == '/';

        if (boundary && path.size() > bestLength)
        {
            best = &_locations[i];
            bestLength = path.size();
        }
    }

    return best;
}
void ServerBlock::applyLocationDefaults()
{
    for (std::size_t i = 0; i < _locations.size(); ++i)
    {
        _locations[i].inheritClientMaxBodySize(
            _clientMaxBodySize);
    }
}