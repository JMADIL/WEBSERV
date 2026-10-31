#ifndef CORELIMITS_HPP
# define CORELIMITS_HPP
# include <cstddef>
struct CoreLimits
{
    std::size_t maxClients;
    std::size_t maxListeners;
    std::size_t maxCgiJobs;
    std::size_t bufferBudget;
    std::size_t maxCgiOutput;
    CoreLimits() : maxClients(1024), maxListeners(64), maxCgiJobs(16),
        bufferBudget(64u * 1024u * 1024u), maxCgiOutput(8u * 1024u * 1024u) {}
};
#endif
