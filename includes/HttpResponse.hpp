#pragma once

#include <string>
#include <map>
#include <vector>

class HttpResponse
{
    public:

        int             statusCode;
        std::string     statusText;

        std::string     headers;
        
        std::vector<char>  body;

        HttpResponse();
        ~HttpResponse();

        void        setStatus(int code);
        void        setHeader(const std::string& key, const std::string& value);
        void        setBody(const std::string& content);
        void        setBody(const std::vector<char>& content);
        std::string serialize() const;
        void        reset();

    private:
        static std::string _getStatusText(int code);
    
};