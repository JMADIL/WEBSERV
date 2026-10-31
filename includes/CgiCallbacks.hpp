#ifndef CGICALLBACKS_HPP
# define CGICALLBACKS_HPP
# include <string>
class Client;
// Implemented by the HTTP/CGI owner. Callbacks queue a response; they do not do I/O.
class CgiCallbacks
{
public:
    virtual ~CgiCallbacks() {}
    virtual void onCgiComplete(Client& client, const std::string& output, int waitStatus) = 0;
    virtual void onCgiError(Client& client, int httpStatus) = 0;
};
#endif
