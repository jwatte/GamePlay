#if !defined(SpacePlayers_h)
#define SpacePlayers_h

#include <map>
#include <vector>


namespace space
{
    using namespace gameplay;

    class SpacePlayer;
    class SpacePlayers;
    class UdpPlayer;
    class UdpServer;
    class SpacePlayersImpl;

    class SpacePlayers
    {
        public:
            SpacePlayers();
            ~SpacePlayers();

            SpacePlayer *findPlayerByUdpPlayer(UdpPlayer *udp);
            SpacePlayer *findPlayerById(size_t id);
            SpacePlayer *getOrMakePlayer(UdpPlayer *udp);
            void getAllPlayers(std::vector<SpacePlayer *> &ovec);
            void destroyPlayersNotInList(std::vector<SpacePlayer *> const &ivec);
            void setForm(Form *f);
            void update(float elapsed);

        private:
            friend class SpacePlayer;
            friend class SpacePlayersImpl;

            std::map<UdpPlayer *, SpacePlayer *> playersByUdp_;
            std::map<size_t, SpacePlayer *> playersById_;
            Form *form_;
            SpacePlayersImpl *impl_;
            UdpServer *udp_;
    };

    class SpacePlayer {
        public:
            char const *name();
            size_t id();
            UdpPlayer *udp();
            void destroy();

        private:
            friend class SpacePlayers;

            SpacePlayer(UdpPlayer *udp, SpacePlayers *ply);
            ~SpacePlayer();

            UdpPlayer *udp_;
            SpacePlayers *players_;
    };
}

#endif  //  SpacePlayers_h
