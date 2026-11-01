#include "LocationBlock.hpp"

LocationBlock::LocationBlock()
    : _index("index.html"),
      _autoindex(false),
      _redirectCode(0),
      _clientMaxBodySize(1024 * 1024),
      _hasBodySizeOverride(false)
{
    _allowedMethods.push_back("GET");
}
const std::string& LocationBlock::getPath() const
{
    return _path;
}

const std::string& LocationBlock::getRoot() const
{
    return _root;
}

const std::vector<std::string>& LocationBlock::getAllowedMethods() const
{
    return _allowedMethods;
}
void LocationBlock::setPath(const std::string& path)
{
    _path = path;
}

void LocationBlock::setRoot(const std::string& root)
{
    _root = root;
}

void LocationBlock::setAllowedMethods(
    const std::vector<std::string>& methods)
{
    _allowedMethods = methods;
}
const std::string& LocationBlock::getIndex() const
{
    return _index;
}

bool LocationBlock::getAutoindex() const
{
    return _autoindex;
}

void LocationBlock::setIndex(const std::string& index)
{
    _index = index;
}

void LocationBlock::setAutoindex(bool autoindex)
{
    _autoindex = autoindex;
}
int LocationBlock::getRedirectCode() const
{
    return _redirectCode;
}

const std::string& LocationBlock::getRedirectURL() const
{
    return _redirectURL;
}

void LocationBlock::setRedirect(
    int code, const std::string& url)
{
    _redirectCode = code;
    _redirectURL = url;
}
const std::string& LocationBlock::getUploadPath() const
{
    return _uploadPath;
}

void LocationBlock::setUploadPath(const std::string& path)
{
    _uploadPath = path;
}
const std::map<std::string, std::string>&
LocationBlock::getCgiExtension() const
{
    return _cgiExtension;
}

void LocationBlock::setCgiExtension(const std::string& extension, const std::string& interpreter)
{
    _cgiExtension[extension] = interpreter;
}
std::size_t LocationBlock::getClientMaxBodySize() const
{
    return _clientMaxBodySize;
}

bool LocationBlock::hasBodySizeOverride() const
{
    return _hasBodySizeOverride;
}

void LocationBlock::setClientMaxBodySize(std::size_t bytes)
{
    _clientMaxBodySize = bytes;
    _hasBodySizeOverride = true;
}

void LocationBlock::inheritClientMaxBodySize(std::size_t bytes)
{
    if (!_hasBodySizeOverride)
        _clientMaxBodySize = bytes;
}