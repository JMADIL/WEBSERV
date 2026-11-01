#include "ResponseBuilder.hpp"
#include "CgiHandler.hpp"

// Define the existing HTTP hook here so the imported HTTP source stays unchanged.
void ResponseBuilder::_handleCGI(Client& client, const LocationBlock& location, const ServerBlock& server)
{
    (void)server;
    CgiHandler::handle(client, location);
}
