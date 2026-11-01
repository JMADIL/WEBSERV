#ifndef CGIRESPONSE_HPP
# define CGIRESPONSE_HPP
# include "CgiCallbacks.hpp"

// Stateless callbacks may safely outlive every job and runtime instance.
class CgiResponse : public CgiCallbacks
{
public:
    void onCgiComplete(Client& client, const std::string& output, int waitStatus);
    void onCgiError(Client& client, int httpStatus);
};
#endif
