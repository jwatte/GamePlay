#include <gameplay.h>
#include "JoinGameMode.h"
#include "SpaceAdventures.h"


namespace space {

    JoinGameMode::JoinGameMode(SpaceAdventures *game) :
        SpaceMode(game)
    {
    }

    void JoinGameMode::enter()
    {
        SpaceAdventures::setNetworkMode(ClientNetwork);
    }
}