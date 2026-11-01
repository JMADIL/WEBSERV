#ifndef LOCATIONBLOCK_HPP
#define LOCATIONBLOCK_HPP

#include <string>
#include <vector>
#include <map>
#include <cstddef>
class LocationBlock
{
    private:
        std::string _path;
        std::string _root;
        std::vector<std::string>    _allowedMethods;
        std::string _index;
        bool    _autoindex;
        int         _redirectCode;
        std::string _redirectURL;
        std::string _uploadPath;
        std::map<std::string, std::string> _cgiExtension;
        std::size_t _clientMaxBodySize;
        bool        _hasBodySizeOverride;

    public:
        LocationBlock();

        const std::string& getPath() const;
        const std::string& getRoot() const;
        const std::vector<std::string>& getAllowedMethods() const;
        void setPath(const std::string& path);
        void setRoot(const std::string& root);
        void setAllowedMethods(const std::vector<std::string>& methods);
        const std::string& getIndex() const;
        bool getAutoindex() const;
        void setIndex(const std::string& index);
        void setAutoindex(bool autoindex);
        int getRedirectCode() const;
        const std::string& getRedirectURL() const;
        void setRedirect(int code, const std::string& url);
        const std::string& getUploadPath() const;
        void setUploadPath(const std::string& path);
        const std::map<std::string, std::string>& getCgiExtension() const;
        void setCgiExtension(const std::string& extension, const std::string& interpreter);
        std::size_t getClientMaxBodySize() const;
        bool hasBodySizeOverride() const;
        void setClientMaxBodySize(std::size_t bytes);
        void inheritClientMaxBodySize(std::size_t bytes);

};
#endif
