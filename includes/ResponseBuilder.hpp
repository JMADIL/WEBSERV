#ifndef RESPONSEBUILDER_HPP
# define RESPONSEBUILDER_HPP
class Client;
class Config;
class ResponseBuilder
{
public:
    static void buildResponse(Client& client, const Config& config);
    static void buildErrorResponse(Client& client, int status, const Config* config = 0);
};
#endif
