#pragma once

#include "PCH.h"
#include "Config.h"

namespace MorphSyncTogether::StrServerDiscovery
{
    struct ClientState
    {
        std::optional<Config::RemotePeer> remotePeer;
        std::optional<std::string> password;
        std::string rawAddress;
    };

    ClientState ReadClientState(std::uint16_t morphSyncPort);
    std::optional<std::string> ReadServerPasswordFromConfig();
}
