#ifndef CONFIG_HPP
# define CONFIG_HPP

# include "ServerBlock.hpp"
# include <string>
# include <vector>

class Config
{
public:
    void load(const std::string& filePath);
    const std::vector<ServerBlock>& getServers() const;

private:
    std::vector<ServerBlock> _servers;
};

#endif
