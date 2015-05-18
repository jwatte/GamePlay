#include <gameplay.h>
#include "SpacePlayers.h"
#include "UdpCommunicator.h"
#include "SpaceAdventures.h"



namespace space
{

    class SpacePlayersImpl : public UdpPlayerFilter
    {
        public:
            SpacePlayersImpl(SpacePlayers *spp);
            void considerNewPlayer(char const *playerName, char const *playerPassword, UdpAddress const &addr) override;

            SpacePlayers *spp_;
    };

    SpacePlayersImpl::SpacePlayersImpl(SpacePlayers *spp) :
        spp_(spp)
    {
    }

    void SpacePlayersImpl::considerNewPlayer(char const *playerName, char const *playerPassword, UdpAddress const &addr)
    {
        UdpPlayer *up = spp_->udp_->addPlayer(playerName, addr, nullptr);
        if (up && !up->cookie())
        {
            SpacePlayer *sp = spp_->getOrMakePlayer(up);
        }
    }


    SpacePlayers::SpacePlayers() :
        form_(nullptr)
    {
        udp_ = SpaceAdventures::udpServer();
        impl_ = new SpacePlayersImpl(this);
        udp_->startServer(SPACE_ADVENTURES_PORT, impl_);
    }

    SpacePlayers::~SpacePlayers()
    {
    }

    void SpacePlayers::setForm(Form *f)
    {
        form_ = f;
    }

    SpacePlayer *SpacePlayers::findPlayerByUdpPlayer(UdpPlayer *udp)
    {
        auto &ptr(playersByUdp_.find(udp));
        if (ptr == playersByUdp_.end())
        {
            return nullptr;
        }
        return (*ptr).second;
    }

    SpacePlayer *SpacePlayers::findPlayerById(size_t id)
    {
        auto &ptr(playersById_.find(id));
        if (ptr == playersById_.end())
        {
            return nullptr;
        }
        return (*ptr).second;
    }

    SpacePlayer *SpacePlayers::getOrMakePlayer(UdpPlayer *udp)
    {
        auto &ptr(playersByUdp_.find(udp));
        if (ptr != playersByUdp_.end())
        {
            return (*ptr).second;
        }
        SpacePlayer *sp = new SpacePlayer(udp, this);
        udp->setCookie(sp);
        return sp;
    }

    void SpacePlayers::getAllPlayers(std::vector<SpacePlayer *> &ovec)
    {
        for (auto &ptr : playersById_)
        {
            ovec.push_back(ptr.second);
        }
    }

    void SpacePlayers::destroyPlayersNotInList(std::vector<SpacePlayer *> const &ivec)
    {
        std::set<SpacePlayer *> all;
        for (auto &ptr : playersById_)
        {
            all.insert(ptr.second);
        }
        for (auto &ptr : ivec)
        {
            all.erase(ptr);
        }
        for (auto &ptr : all)
        {
            ptr->destroy();
        }
    }

    void SpacePlayers::update(float elapsed)
    {
    }



    char const *SpacePlayer::name()
    {
        return udp_->name();
    }

    size_t SpacePlayer::id()
    {
        return udp_->playerId();
    }

    UdpPlayer *SpacePlayer::udp()
    {
        return udp_;
    }

    void SpacePlayer::destroy()
    {
        players_->playersByUdp_.erase(players_->playersByUdp_.find(udp_));
        players_->playersById_.erase(players_->playersById_.find(udp_->playerId()));
        delete this;
    }

    SpacePlayer::SpacePlayer(UdpPlayer *udp, SpacePlayers *ply) :
        udp_(udp),
        players_(ply)
    {
    }

    SpacePlayer::~SpacePlayer()
    {
    }


}