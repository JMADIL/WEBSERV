#include "ResponseBuilder.hpp"
#include "CgiHandler.hpp"
#include "CgiResponse.hpp"
#include "Client.hpp"
#include <new>

void CgiHandler::handle(Client& client, const LocationBlock& location)
{
    if (client.getState() != STATE_BUILDING_RESPONSE || client.getSentOffset() != 0)
        throw std::logic_error("CGI routing requires an unsent response under construction");
    // Stateless process-lifetime callbacks remain valid through cancellation and shutdown.
    static CgiResponse callbacks;
    client.setCloseAfterResponse(!client.getRequest().keepAlive());
    try {
        Target target;
        if (!resolve(client.getRequest().uri, location, target)) {
            callbacks.onCgiError(client, 404);
            return;
        }
        launch(client, location, callbacks);
    } catch (const LaunchError& error) { callbacks.onCgiError(client, error.status()); }
    catch (const std::invalid_argument&) { callbacks.onCgiError(client, 400); }
    catch (const std::bad_alloc&) { callbacks.onCgiError(client, 503); }
    catch (const std::exception&) { callbacks.onCgiError(client, 500); }
}

// Define the existing HTTP hook here so the imported HTTP source stays unchanged.
void ResponseBuilder::_handleCGI(Client& client, const LocationBlock& location, const ServerBlock& server)
{
    (void)server;
    CgiHandler::handle(client, location);
}
