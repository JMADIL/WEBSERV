#ifndef SESSION_HPP
# define SESSION_HPP

# include <string>
# include <map>
# include <ctime>

class Session
{
    public:
        std::string                         id;
        std::map<std::string, std::string>  data;
        time_t                              createdAt;
        time_t                              lastAccessedAt;

        Session();
        Session(const std::string &SessionId);
        ~Session();

        void set(const std::string &key, const std::string &value);
        std::string get(const std::string &key) const;
        bool        has(const std::string &key) const;
        void        touch(); // Updates lastAccessedAt to current time
        bool        isExpired(time_t maxLifetimeSec) const;
};

#endif