#include "Session.hpp"

Session::Session() : createdAT(time(NULL)), lastAccessedAt(time(NULL))
{
}

Session::Session(const std::string &SessionId) : id(SessionId), createdAt(time(NULL)), lastAccessedAt(time(NULL))
{
}

Session::~Session()
{
}

void Session::set(const std::string &key, const std::string &value)
{
    data[key] = value;
    lastAccessedAt = time(NULL);
}

std::string Session::get(const std::string &key) const
{
    std::map<std::string, std::string>::const_iterator it = data.find(key);
    if (it != data.end())
        return it->second;
    return "";
}

bool Session::has(const std::string &key) const
{
    return (data.find(key) != data.end());
}

void Session::touch()
{
    lastAccessedAt = time(NULL);
}

bool Session::isExpired(time_t maxLifetimeSec) const
{
    time_t now = time(NULL);
    return (difftime(now, lastAccessedAt) > maxLifetimeSec);
}