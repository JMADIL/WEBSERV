#include "Config.hpp"
#include "Server.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <new>

int main(int argc, char** argv)
{
    try
    {
        if (argc > 2)
        {
            std::cerr << "Usage: ./webserv [configuration file]\n";
            return EXIT_FAILURE;
        }

        const char* path = argc == 2 ? argv[1] : "configs/default.conf";
        Config config;
        config.load(path);
        Server server(config);
        server.initializeListeners();

        std::cout << "Configuration loaded: " << path << '\n';
        const std::vector<ServerBlock>& servers = config.getServers();
        for (std::size_t i = 0; i < servers.size(); ++i)
        {
            std::cout << "  Configured endpoint: " << servers[i].getHost()
                      << ':' << servers[i].getPort()
                      << " (" << servers[i].getLocations().size()
                      << " locations)\n";
        }
        std::cout << "Server ready. Press Ctrl-C to stop.\n";
        std::cout.flush();
        if (!std::cout)
        {
            std::cerr << "webserv: could not write configuration summary\n";
            return EXIT_FAILURE;
        }
        server.run();
        return EXIT_SUCCESS;
    }
    catch (const std::bad_alloc&)
    {
        std::cerr << "webserv: insufficient memory during startup\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "webserv: " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "webserv: unexpected startup failure\n";
    }
    return EXIT_FAILURE;
}
