#if !defined(ServerSetupMode_h)
#define ServerSetupMode_h

#include "SpaceMode.h"
#include "SpacePlayers.h"


namespace space
{
    using namespace gameplay;

    class ServerSetupMode : public SpaceMode
    {
        public:
            ServerSetupMode(SpaceAdventures *game, SpacePlayers *spp, char const *sessionName, unsigned char difficulty, unsigned char clutter);
            ~ServerSetupMode();

            void enter() override;
            void leave() override;
            void update(float elapsed);
            void render(float elapsed);

            void start();
            void back();
            void kick();
            void chat();

            std::string sessionName_;
            unsigned char difficulty_;
            unsigned char clutter_;
            SpacePlayers *spp_;
            Form *form_;
    };
}

#endif  //  ServerSetupMode_h
