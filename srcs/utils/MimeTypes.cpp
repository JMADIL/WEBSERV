#include "MimeTypes.hpp"

std::map<std::string, std::string> MimeTypes::_types;
bool MimeTypes::_initialized = false;

void MimeTypes::_initMap()
{
    _types[".html"] = "text/html";
    _types[".htm"]  = "text/html";
    _types[".css"]  = "text/css";
    _types[".js"]   = "application/javascript";
    _types[".json"] = "application/json";
    _types[".xml"]  = "application/xml";
    _types[".txt"]  = "text/plain";
    _types[".png"]  = "image/png";
    _types[".jpg"]  = "image/jpeg";
    _types[".jpeg"] = "image/jpeg";
    _types[".gif"]  = "image/gif";
    _types[".svg"]  = "image/svg+xml";
    _types[".ico"]  = "image/x-icon";
    _types[".pdf"]  = "application/pdf";
    _types[".zip"]  = "application/zip";
    _types[".mp4"]  = "video/mp4";
    _types[".webp"] = "image/webp";
    _initialized = true;
}

std::string MimeTypes::getType(const std::string &filepath)
{
    if (!_initialized)
        _initMap();

    size_t dotPos = filepath.rfind('.');
    if (dotPos != std::string::npos)
    {
        std::string ext = filepath.substr(dotPos);
        std::map<std::string, std::string>::const_iterator it = _types.find(ext);
        if (it != _types.end())
            return it->second;
    }
    return "application/octet-stream";
}


