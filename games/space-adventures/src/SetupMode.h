#if !defined(SetupMode_h)
#define SetupMode_h

#include "SpaceMode.h"
#include <Form.h>


namespace space {

     using namespace gameplay;

     class SetupMode : public SpaceMode {
        public:
            SetupMode(SpaceAdventures *game);
            ~SetupMode();

            virtual void enter() override;
            virtual void leave() override;

            virtual void update(float elapsed) override;
            virtual void render(float elapsed) override;
            virtual void keyEvent(Keyboard::KeyEvent evt, int key) override;
            virtual void touchEvent(Touch::TouchEvent evt, int x, int y, unsigned int contactIndex) override;

            void join();
            void host();
            void back();

        private:
            Form *form_;
     };
}

#endif  //  SetupMode_h
