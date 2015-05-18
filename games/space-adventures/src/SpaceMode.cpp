#include <gameplay.h>
#include "SpaceMode.h"
#include "SpaceAdventures.h"

#include <Scene.h>
#include <Node.h>
#include <Model.h>
#include <Material.h>

namespace space {

    using namespace gameplay;

    SpaceMode::SpaceMode(SpaceAdventures *game) :
        game_(game)
    {
    }

    SpaceMode::~SpaceMode()
    {
    }

    void SpaceMode::enter()
    {
    }

    void SpaceMode::leave()
    {
    }


    void SpaceMode::update(float elapsedTime)
    {
    }

    void SpaceMode::render(float elapsedTime)
    {
    }

    void SpaceMode::keyEvent(Keyboard::KeyEvent evt, int key)
    {
    }

    void SpaceMode::touchEvent(Touch::TouchEvent evt, int x, int y, unsigned int contactIndex)
    {
    }

}
