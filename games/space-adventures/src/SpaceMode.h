#if !defined(SpaceMode_h)
#define SpaceMode_h

#include <Ref.h>
#include <Keyboard.h>
#include <Touch.h>
#include <Scene.h>

namespace space {

    using namespace gameplay;

    class SpaceAdventures;

    class HoldForm {
        public:
            HoldForm(Form *f) : f_(f) {
                f_->addRef();
            }
            ~HoldForm() {
                f_->release();
            }
            HoldForm(HoldForm const &) = delete;
            HoldForm &operator=(HoldForm const &) = delete;
            Form *f_;
    };
    class SpaceMode : public Ref {
        public:
            SpaceMode(SpaceAdventures *game);
            virtual ~SpaceMode();

            virtual void enter();
            virtual void leave();

            virtual void update(float elapsed);
            virtual void render(float elapsed);
            virtual void keyEvent(Keyboard::KeyEvent evt, int key);
            virtual void touchEvent(Touch::TouchEvent evt, int x, int y, unsigned int contactIndex);

        protected:
            SpaceAdventures *game_;
    };
}

#endif  //  SpaceMode_H
