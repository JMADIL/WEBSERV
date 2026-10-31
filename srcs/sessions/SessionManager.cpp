#include "SessionManager.hpp"
#include <sstream>
#include <cstdlib>
#include <ctime>

SessionManager::SessionManager(time_t sessionLifetime) : _sessionLifetime(sessionLifetime)
{
    std::srand(static_cast<unsigned int>(time(NULL)));
}

SessionManager::~SessionManager()
{
}

std::string SessionManager::_generateSessionId()
{
    static const char hexChars[] = "0123456789abcdef";
    std::string id = "sess_";

    std::ostringstream oss;
    oss << std::hex << time(NULL);
    id += oss.str() + "_";

    for(int i = 0; i < 16; ++i)
    {
        id += hexChars[std::rand() % 16];
    }
    return id;
}

Session* SessionManager::createSession()
{
    cleanExpiredSessions();
    std::string id = _generateSessionId();
    _sessions[id] = Session(id);
    return &_sessions[id];
}

Session* SessionManager::getSession(const std::string &sessionId)
{
    if (sessionId.empty())
        return NULL;

    std::map<std::string, Session>::iterator it = _sessions.find(sessionId);
    if (it != _sessions.end())
    {
        if (it->second.isExpired(_sessionLifetime))
        {
            _sessions.erase(it);
            return NULL;
        }
        it->second.touch();
        return &(it->second);
    }
    return NULL;
}

void SessionManager::destroySession(const std::string &sessionId)
{
    _sessions.erase(sessionId);
}

void SessionManager::cleanExpiredSessions()
{
    std::map<std::string, Session>::iterator it = _sessions.begin();

    while(it != _sessions.end())
    {
        if(it->second.isExpired(_sessionLifetime))
        {
            std::map<std::string, Session>::iterator toErase = it;
            ++it;
            _sessions.erase(toErase);
        }
        else
        {
            ++it;
        }
    }
}