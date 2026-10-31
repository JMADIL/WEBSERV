#ifndef SESSIONMANAGER_HPP
# define SESSIONMANAGER_HPP

# include "Session.hpp"
# include <string>
# include <map>

class SessionManager
{
    public:
        SessionManager(time_t sessionLifetime = 1800);
        ~SessionManager();

        Session*    createSession();
        Session*    getSession(const std::string &sessionId);
        void        destroySession(const std::string &sessionId);
        void        cleanExpiredSessions();

    private:

        std::map<std::string, Session>  _sessions;
        time_t                          _sessionLifetime;

        std::string _generateSessionId();
};