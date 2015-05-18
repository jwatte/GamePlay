#if !defined(JoinGameMode_h)
#define JoinGameMode_h

#include "SpaceMode.h"

namespace space {
    using namespace gameplay;

    class JoinGameMode : public SpaceMode {
    public:
        JoinGameMode(SpaceAdventures *game);
        void enter() override;
    };
}

#endif  //  JoinGameMode_h
