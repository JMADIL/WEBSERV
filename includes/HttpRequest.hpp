#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <map>
# include <string>
# include <vector>

class HttpRequest
{
    public:

        std::string     method;  //bhal "GET", "POST" & "DELETE"
        std::string     uri;      //"/path/to/resource"
        std::string     querystring;       // "key=val&key2=val2"
        std::string     version;    // "HTTP/1.1"

        std::map<std::string, std::string> headers;

        std::vector<char>                   body;
        size_t                              contentLength;
        bool                                isChunked;
    
        HttpRequest();
        ~HttpRequest();
    
        void        reset();
        std::string getHeader(const std::string &key) const;
        bool        hasHeader(const std::string &key) const;
        bool        keepAlive() const;
        std::string getCookie(const std::string &name) const;
};

#endif