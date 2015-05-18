#if !defined(SpaceShared_h)
#define SpaceShared_h

#include <Font.h>

namespace space {

    using namespace gameplay;

    class SpaceShared {
        public:
            static void load();
            static void unload();

            static Font *littleFont();
            static Font *mediumFont();
            static Font *bigFont();

        private:
            static Font *little_;
            static Font *medium_;
            static Font *big_;
    };
}

#endif  //  SpaceShared_h
