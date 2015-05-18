#include <gameplay.h>
#include "HostGameMode.h"
#include "SpaceAdventures.h"
#include "UdpCommunicator.h"
#include "ServerSetupMode.h"


namespace space {

    using namespace gameplay;

    class HostGameAction : public Control::Listener {
    public:
        HostGameAction(HostGameMode *m, void (HostGameMode::*f)()) : m_(m), f_(f) {}
        void controlEvent(Control* control, EventType evt) override
        {
            (m_->*f_)();
        }
        HostGameMode *m_;
        void (HostGameMode::*f_)();
    };

    HostGameMode::HostGameMode(SpaceAdventures *game) :
        SpaceMode(game),
        spp_(nullptr)
    {
        form_ = Form::create("res/ui/host.form");
        form_->getControl("backButton")->addListener(new HostGameAction(this, &HostGameMode::back), Control::Listener::CLICK);
        form_->getControl("nextButton")->addListener(new HostGameAction(this, &HostGameMode::next), Control::Listener::CLICK);
    }

    HostGameMode::~HostGameMode()
    {
        SAFE_DELETE(spp_);
        SAFE_RELEASE(form_);
    }

    void HostGameMode::enter()
    {
        SpaceAdventures::setNetworkMode(ServerNetwork);
        form_->setEnabled(true);
        if (!spp_)
        {
            spp_ = new SpacePlayers();
        }
        spp_->setForm(form_);
    }

    void HostGameMode::leave()
    {
        form_->setEnabled(false);
    }

    void HostGameMode::update(float elapsed)
    {
        form_->update(elapsed);
        spp_->update(elapsed);
        std::string sessionName(static_cast<TextBox *>(form_->getControl("nameText"))->getText());
        float difficulty = static_cast<Slider *>(form_->getControl("difficultySlider"))->getValue();
        float clutter = static_cast<Slider *>(form_->getControl("clutterSlider"))->getValue();
        UdpGameParams gp;
        gp.gameMode = 1;
        gp.gameState = 1;
        gp.maxPlayers = 6;
        gp.numPlayers = SpaceAdventures::udpServer()->numPlayers();
        gp.params[0] = (unsigned char)difficulty;
        gp.params[1] = (unsigned char)clutter;
        SpaceAdventures::udpServer()->startAdvertising(sessionName.c_str(), gp);
        SpaceAdventures::udpServer()->update(elapsed);
    }

    void HostGameMode::render(float elapsed)
    {
        form_->draw(false);
    }

    void HostGameMode::keyEvent(Keyboard::KeyEvent evt, int key)
    {
        if (evt == Keyboard::KEY_PRESS)
        {
            if (key == Keyboard::KEY_ESCAPE)
            {
                HoldForm hf(form_);
                back();
                return;
            }
        }
    }

    void HostGameMode::back()
    {
        game_->popMode();
    }

    void HostGameMode::next()
    {
        std::string sessionName(static_cast<TextBox *>(form_->getControl("nameText"))->getText());
        float difficulty = static_cast<Slider *>(form_->getControl("difficultySlider"))->getValue();
        float clutter = static_cast<Slider *>(form_->getControl("clutterSlider"))->getValue();
        game_->pushMode(new ServerSetupMode(game_, spp_, sessionName.c_str(), difficulty, clutter));
    }

}
