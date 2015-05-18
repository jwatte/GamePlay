#if !defined(HostGameMode_h)
#define HostGameMode_h

#include "SpaceMode.h"

namespace space {
    using namespace gameplay;

    class UdpServer;
    class SpacePlayers;

    class HostGameMode : public SpaceMode {
        public:
            HostGameMode(SpaceAdventures *game);
            ~HostGameMode();
            void enter() override;
            void leave() override;

            void HostGameMode::update(float elapsed) override;
            void HostGameMode::render(float elapsed) override;
            void HostGameMode::keyEvent(Keyboard::KeyEvent evt, int key) override;

            void next();
            void back();

            Form *form_;
            SpacePlayers *spp_;
    };
}

#endif  //  HostGameMode_h
