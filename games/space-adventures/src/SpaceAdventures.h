#ifndef SpaceAdventures_H_
#define SpaceAdventures_H_

#include <gameplay.h>
#include <list>

namespace space {

    #define SPACE_ADVENTURES_PORT 10170

    enum
    {
        CMD_CHAT = 0x01
    };

    using namespace gameplay;
    class SpaceMode;
    class UdpClient;
    class UdpServer;

    enum SpaceNetworkMode {
        NoNetwork,
        ClientNetwork,
        ServerNetwork
    };
    class SpaceAdventures: public Game
    {
    public:

        SpaceAdventures();

	    void keyEvent(Keyboard::KeyEvent evt, int key) override;
        void touchEvent(Touch::TouchEvent evt, int x, int y, unsigned int contactIndex) override;

        void pushMode(SpaceMode *newMode);
        void popMode();

        static void setNetworkMode(SpaceNetworkMode mode);
        static UdpClient *udpClient();
        static UdpServer *udpServer();

    protected:

        void initialize() override;
        void finalize() override;
        void update(float elapsedTime) override;
        void render(float elapsedTime) override;

    private:

        SpaceMode *curMode_;
        std::list<SpaceMode *> stack_;
        static UdpClient *ucli_;
        static UdpServer *usrv_;
    };

}

#endif
