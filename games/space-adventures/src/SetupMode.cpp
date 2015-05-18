#include "SetupMode.h"
#include <Keyboard.h>
#include <Touch.h>
#include <Control.h>

#include "SpaceAdventures.h"
#include "JoinGameMode.h"
#include "HostGameMode.h"


namespace space {

    using namespace gameplay;

    class SetupAction : public Control::Listener {
        public:
            SetupAction(SetupMode *m, void (SetupMode::*f)()) : m_(m), f_(f) {}
            void controlEvent(Control* control, EventType evt) override
            {
                (m_->*f_)();
            }
            SetupMode *m_;
            void (SetupMode::*f_)();
    };

    SetupMode::SetupMode(SpaceAdventures *game) :
        SpaceMode(game),
        form_(nullptr)
    {
        form_ = Form::create("res/ui/setup.form");
        form_->getControl("joinButton")->addListener(new SetupAction(this, &SetupMode::join), Control::Listener::CLICK);
        form_->getControl("hostButton")->addListener(new SetupAction(this, &SetupMode::host), Control::Listener::CLICK);
        form_->getControl("backButton")->addListener(new SetupAction(this, &SetupMode::back), Control::Listener::CLICK);
    }

    SetupMode::~SetupMode()
    {
        SAFE_RELEASE(form_);
    }

    void SetupMode::enter()
    {
        SpaceAdventures::setNetworkMode(NoNetwork);
        form_->setEnabled(true);
    }

    void SetupMode::leave()
    {
        form_->setEnabled(false);
    }

    void SetupMode::update(float elapsed)
    {
        HoldForm hf(form_);
        form_->update(elapsed);
    }

    void SetupMode::render(float elapsed)
    {
        form_->draw(false);
    }

    void SetupMode::keyEvent(Keyboard::KeyEvent evt, int key)
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

    void SetupMode::touchEvent(Touch::TouchEvent evt, int x, int y, unsigned int contactIndex)
    {

    }

    void SetupMode::join()
    {
        game_->pushMode(new JoinGameMode(game_));
    }

    void SetupMode::host()
    {
        game_->pushMode(new HostGameMode(game_));
    }

    void SetupMode::back()
    {
        game_->popMode();
    }
}
