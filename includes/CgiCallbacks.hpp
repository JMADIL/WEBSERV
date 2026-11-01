#ifndef CGICALLBACKS_HPP
# define CGICALLBACKS_HPP
# include <string>
class Client;
// Callbacks queue a response or a deferred local redirect; no I/O or Server reentry.
class CgiCallbacks
{
public:
    virtual ~CgiCallbacks() {}
    virtual void onCgiComplete(Client& client, const std::string& output, int waitStatus) = 0;
    virtual void onCgiError(Client& client, int httpStatus) = 0;
};
#endif
