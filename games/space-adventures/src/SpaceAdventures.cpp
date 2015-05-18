#include "SpaceAdventures.h"
#include "SpaceShared.h"
#include "SetupMode.h"
#include "UdpCommunicator.h"


using namespace space;

SpaceAdventures game;


SpaceAdventures::SpaceAdventures()
    : curMode_(NULL)
{
    UdpCommunicator::init();
}

void SpaceAdventures::initialize()
{
    SpaceShared::load();
    pushMode(new SetupMode(this));
}

void SpaceAdventures::pushMode(SpaceMode *m)
{
    if (curMode_)
    {
        curMode_->leave();
        stack_.push_back(curMode_);
    }
    curMode_ = m;
    m->addRef();
    curMode_->enter();
}

void SpaceAdventures::popMode()
{
    curMode_->leave();
    SAFE_RELEASE(curMode_);
    if (!stack_.empty())
    {
        curMode_ = stack_.back();
        stack_.pop_back();
    }
    if (!curMode_)
    {
        this->exit();
    }
    else
    {
        curMode_->enter();
    }
}

void SpaceAdventures::finalize()
{
    SAFE_RELEASE(curMode_);
}

void SpaceAdventures::update(float elapsedTime)
{
    if (ucli_)
    {
        ucli_->update(elapsedTime);
    }
    if (usrv_)
    {
        usrv_->update(elapsedTime);
    }
    curMode_->update(elapsedTime);
}

void SpaceAdventures::render(float elapsedTime)
{
    // Clear the color and depth buffers
    clear(CLEAR_COLOR_DEPTH, Vector4::zero(), 1.0f, 0);
    curMode_->render(elapsedTime);
}

void SpaceAdventures::keyEvent(Keyboard::KeyEvent evt, int key)
{
    curMode_->keyEvent(evt, key);
}

void SpaceAdventures::touchEvent(Touch::TouchEvent evt, int x, int y, unsigned int contactIndex)
{
    if (x < 0) { x = 0; }
    if (x >(int)getWidth()) { x = (int)getWidth(); }
    if (y < 0) { y = 0; }
    if (y >(int)getHeight()) { y = (int)getHeight(); }
    curMode_->touchEvent(evt, x, y, contactIndex);
}


UdpClient *SpaceAdventures::ucli_;
UdpServer *SpaceAdventures::usrv_;

void SpaceAdventures::setNetworkMode(SpaceNetworkMode mode)
{
    if (mode == ClientNetwork && ucli_ != NULL)
    {
        return;
    }
    if (mode == ServerNetwork && usrv_ != NULL)
    {
        return;
    }

    SAFE_RELEASE(ucli_);
    SAFE_RELEASE(usrv_);

    switch (mode)
    {
        case ClientNetwork:
            ucli_ = UdpClient::create("Space Adventures");
            break;
        case ServerNetwork:
            usrv_ = UdpServer::create("Space Adventures");
            break;
    }
}

UdpClient *SpaceAdventures::udpClient()
{
    return ucli_;
}

UdpServer *SpaceAdventures::udpServer()
{
    return usrv_;
}

