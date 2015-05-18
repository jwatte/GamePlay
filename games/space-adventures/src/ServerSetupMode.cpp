#include <gameplay.h>
#include "ServerSetupMode.h"
#include "SpaceAdventures.h"
#include "UdpCommunicator.h"
#include "SpacePlayers.h"


namespace space
{
    using namespace gameplay;

    class ServerSetupAction : public Control::Listener {
    public:
        ServerSetupAction(ServerSetupMode *m, void (ServerSetupMode::*f)()) : m_(m), f_(f) {}
        void controlEvent(Control* control, EventType evt) override
        {
            (m_->*f_)();
        }
        ServerSetupMode *m_;
        void (ServerSetupMode::*f_)();
    };


    ServerSetupMode::ServerSetupMode(SpaceAdventures *game, SpacePlayers *spp, char const *sessionName, unsigned char difficulty, unsigned char clutter) :
        SpaceMode(game),
        spp_(spp),
        sessionName_(sessionName),
        difficulty_(difficulty),
        clutter_(clutter)
    {
        form_ = Form::create("res/ui/serversetup.form");
        form_->getControl("backButton")->addListener(new ServerSetupAction(this, &ServerSetupMode::back), Control::Listener::CLICK);
        form_->getControl("startButton")->addListener(new ServerSetupAction(this, &ServerSetupMode::start), Control::Listener::CLICK);
        form_->getControl("chatButton")->addListener(new ServerSetupAction(this, &ServerSetupMode::chat), Control::Listener::CLICK);
        form_->getControl("kickButton")->addListener(new ServerSetupAction(this, &ServerSetupMode::kick), Control::Listener::CLICK);
        form_->getControl("chatText")->addListener(new ServerSetupAction(this, &ServerSetupMode::chat), Control::Listener::ACTIVATED);
    }

    ServerSetupMode::~ServerSetupMode()
    {
        SAFE_RELEASE(form_);
    }

    void ServerSetupMode::enter()
    {
        form_->setEnabled(true);
        spp_->setForm(form_);
        TextBox *tb = static_cast<TextBox *>(form_->getControl("chatText"));
        tb->setFocus();
        std::vector<SpacePlayer *> spp;
        UdpPlayer *up = nullptr;
        UdpServer *us = SpaceAdventures::udpServer();
        for (size_t ix = 0; up = us->peekPlayer(ix); ++ix)
        {
            SpacePlayer *sp = spp_->getOrMakePlayer(up);
            spp.push_back(sp);
        }
        spp_->destroyPlayersNotInList(spp);
    }

    void ServerSetupMode::leave()
    {
        form_->setEnabled(false);
    }

    void ServerSetupMode::update(float elapsed)
    {
        form_->update(elapsed);
        spp_->update(elapsed);
        form_->getControl("startButton")->setEnabled(SpaceAdventures::udpServer()->numPlayers() > 0);
        SpaceAdventures::udpServer()->update(elapsed);
    }

    void ServerSetupMode::render(float elapsed)
    {
        form_->draw();
    }

    void ServerSetupMode::start()
    {

    }

    void ServerSetupMode::back()
    {
        game_->popMode();
    }

    void ServerSetupMode::kick()
    {

    }

    void ServerSetupMode::chat()
    {
        TextBox *tb = static_cast<TextBox *>(form_->getControl("chatText"));
        std::string text(tb->getText());
        tb->setText("");
        tb->setFocus();
        if (text.length()) 
        {
            std::vector<unsigned char> buf;
            unsigned char x[2] = { CMD_CHAT, 0 };
            buf.insert(buf.end(), x, &x[2]);
            buf.insert(buf.end(), text.begin(), text.end());
            buf.push_back(0);
            SpaceAdventures::udpServer()->sendToAll(&buf[0], buf.size());
        }
    }

}