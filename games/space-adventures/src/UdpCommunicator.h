#if !defined(UdpCommunicator_h)
#define UdpCommunicator_h

#include <Ref.h>

namespace space {

    using namespace gameplay;

    /* The PacketCmd is the first byte (or var-int) of a message payload. */
    enum PacketCmd {
        /* the framework reserves commands from 0x60 to 0x7f -- players can use 0 .. 0x5f and 0x80 .. and up */
        CMD_MIN_RESERVED_ID = 0x60,
        CMD_MAX_RESERVED_ID = 0x7f,

        /* client to server */
        CMD_C2S_CONNECT = 0x61,      /* gamename-str, playername-str, password-str */
        CMD_C2S_DISCONNECT = 0x62,   /* nothing */
        CMD_C2S_KNOWPLAYERS = 0x63,  /* id, ... */

        /* server to client */
        CMD_S2C_CONNECTED = 0x61,    /* your-id */
        CMD_S2C_DISCONNECTED = 0x62, /* id, ... */ /* may be you, may be some other player */
        CMD_S2C_PLAYERINFO = 0x63,   /* id, name-str, ... */
    };

    /* simple wrapper for IPv4 and IPv6 addresses. */
    struct UdpAddress {
        UdpAddress();
        /* construct from IPv4 address */
        UdpAddress(struct sockaddr_in const &sin);
        /* dotted-decimal, or colon-quads, or host name -- may take time to resolve */
        UdpAddress(char const *name);

        unsigned short  addrSize;   //  4 or 16
        unsigned short  port;       //  host byte order
        unsigned char   ipaddr[16]; //  network byte order

        static UdpAddress broadcast4(unsigned short port);
        static UdpAddress broadcast6(unsigned short port);

        bool operator==(UdpAddress const &o) const;
        bool operator!=(UdpAddress const &o) const;
        bool operator<(UdpAddress const &o) const;
        bool operator<=(UdpAddress const &o) const;
        bool operator>(UdpAddress const &o) const;
        bool operator>=(UdpAddress const &o) const;

        void formatAddr(char *obuf);    //  obuf must be at least 64 chars!
    };

    /* information about a particular game instance, for use by clients to find games they are interested in. */
    struct UdpGameParams {
        /* UdpServer/UdpClient don't really care about the contents of this -- you can put whatever you want in here */
        UdpGameParams();
        unsigned char   gameMode;
        unsigned char   gameState;
        unsigned char   numPlayers;
        unsigned char   maxPlayers;
        unsigned char   params[12];
    };

    /* information about a game found when scanning the network. */
    struct UdpPossibleGame {
        UdpPossibleGame();
        UdpAddress      address;
        UdpGameParams   params;
        char            sessionName[32];

        bool operator==(UdpPossibleGame const &o) const;
        bool operator<(UdpPossibleGame const &o) const;
        bool operator!=(UdpPossibleGame const &o) const;
    };

    /* various statistics about a connection to a remote endpoint */
    struct UdpStatistics {
        UdpStatistics();
        unsigned int    numPacketsIgnored;
        unsigned int    numPacketsActuallyReceived;
        unsigned int    numPacketsActuallySent;
        unsigned int    numPacketsLostReceived;
        unsigned int    numPacketsLostRecentlyReceived;
        unsigned int    numPacketsLostSent;
        unsigned int    numPacketsLostRecentlySent;
        double          lastPacketReceivedTime;
        float           estimatedRoundTripTimeSeconds;
    };

    /* Implemented by the client application to determine whether a connecting player is suitable to add to the game. */
    class UdpPlayerFilter : public Ref {
        public:
            /* A remote player has requested a connection. It's up to you to call addPlayer(), or rejectPlayer(), or 
               do nothing. You can call addPlayer() at any reasonable time later; don't need to do it from within this 
               callback if you need more time to validate a password or whatever. */
            virtual void considerNewPlayer(char const *playerName, char const *playerPassword, UdpAddress const &addr) = 0;
        protected:
            virtual ~UdpPlayerFilter() {}
    };

    /* An interface used by both UdpClient and UdpPlayer to do actual network I/O. */
    class UdpPacketIO {
        public:
            /* How many packets (messages) are pending input? */
            virtual size_t numPackets() = 0;
            /* What's the size of the first pending packet (or 0, if none.) */
            virtual size_t packetSize() = 0;
            /* Pointer to data of first packet. Or NULL if none. Can retrieve size, too, for convenience*/
            virtual void const *packetData(size_t *osize = 0) = 0;
            /* Remove the first packet. If there are more packets, the "current packet" will then be the next one in the queue. */
            virtual void consumeMessage() = 0;
            /* Queue a packet for sending to the remote end. Packets are always fire-and-forget, although they will not be 
               delivered more than once (zero-or-once) and never out of order (a late packet will be dropped rather than delivered.) 
               There is an upper bound to the per-packet size; at most 1200 bytes can be sent per call to this function. Also, 
               if too much data is enqueued all at once, older messages will be dropped (this is similar to congestion on a network 
               link where packets will be dropped, too.) Assuming the user's network supports it, you're always able to send at least 
               10 kB per second; under perfect circumstances, you can send up to 6 MB per second (but don't do that!) */
            virtual bool sendMessage(void const *data, size_t size) = 0;
            /* If for some reason you really want a physical packet to go out as soon as possible (rather than being scheduled as usual) 
               you can flush the queue. This means a packet will go on the wire the very next time update() is called, no matter 
               what the send-rate-control timers think would be a better idea. */
            virtual void flushOutput() = 0;
            /* Get some statistics on the connection quality */
            virtual void getStats(UdpStatistics &stats) = 0;
        protected:
            virtual ~UdpPacketIO() {}
    };

    class UdpServer;

    /* Implemented by the library, an UdpPlayer represents a particular remote player. */
    class UdpPlayer : public Ref {
        public:
            /* the name used to create this player */
            virtual char const *name() = 0;
            /* the ID of this player */
            virtual size_t playerId() = 0;
            /* the cookie used when creating this player */
            virtual void *cookie() = 0;
            /* the server object that created this player */
            virtual UdpServer *server() = 0;
            /* shutdown this client -- this is not the same as release; you have to do that separately. */
            virtual void shutdownClient() = 0;

            /* to actually receive and send packets, use the packet IO interface */
            virtual UdpPacketIO *io() = 0;

            /* set cookie */
            virtual void setCookie(void *cookie) = 0;

        protected:
            virtual ~UdpPlayer() {}
    };

    class UdpServer : public Ref {
        public:
            /* You typically only use one UdpServer or UdpClient per process, but it is totally possible 
               to use more than one (on different ports.) Each Communicator, and the objects used by it, 
               needs to be run on a single thread; mutliple separate instances can run on different 
               threads. You can also run them in series, all on the main thread. UdpCommunicator does not 
               create a thread for you; typically you'll just call update() within the main game loop. */
            static UdpServer *create(char const *gameName);

            /* Do actual work -- Nothing happens unless update() is called. */
            virtual void update(float elapsedTime) = 0;
            /* How much time has accumulated in update() calls? */
            virtual double accumulatedTime() = 0;

            /* For hosting games, start a server on a port. Make sure the process has permission
               to bind to this port! */
            virtual bool startServer(unsigned short port, UdpPlayerFilter *filter) = 0;
            /* To tell other games on the local network that the game exists, start advertising 
               (which will start a periodic broadcast.) sessionName can have a strlen() of at most 31. 
               Warning: Make sure the process has UDP broadcast sending permission. On some OS-es, this
               may not be allowed by default. */
            virtual bool startAdvertising(char const *sessionName, UdpGameParams const &gp) = 0;
            /* When advertising a game, provide some parameters for a presumed remote game browser. */
            virtual void setAdvertisingParameters(UdpGameParams const &gp) = 0;
            /* To stop periodically broadcasting on the local network. */
            virtual void stopAdvertising() = 0;
            /* Get the local interface address. Suitable for telling local players about. Fails if not 
               yet known. */
            virtual bool getPrivateGameAddress(UdpPossibleGame &ogame) = 0;
            /* Shutdown the server, remove all currently active clients, stop advertising. You can then 
               start a new server session. */
            virtual void shutdownServer() = 0;
            /* return true if currently serving. */
            virtual bool isServing() = 0;
            /* return true if currently advertising. */
            virtual bool isadvertising() = 0;

            /* If your filter likes a particular incoming client request, it can add that client to the 
               set of actively connected clients. This can be done asynchronously outside the filter callback 
               function, to allow for asynchronous lookup of name/password or whatever. This class keeps 
               one refcount on the client, and returns adds another refcount for you. Remove the refcount 
               when you are done; the client will live on. The client will not go away until you call its 
               shutdown() method. The primary key for the client list is the UdpAddress, so trying to add 
               a second client with the same UdpAddress as a first, will return NULL. */
            virtual UdpPlayer *addPlayer(char const *clientName, UdpAddress const &addr, void *cookie) = 0;
            /* Peek to see if a given player exists based on its UdpAddress. This does NOT acquire another 
               refcount -- you "borrow" or "peek at" it. */
            virtual UdpPlayer *peekPlayer(UdpAddress const &addr) = 0;
            /* If you want to explicitly reject a client, so that it doesn't keep sending packets to you,
               call rejectClient() with that address. This will send a rejection packet to the client, 
               and will then keep it on a black list until you stop advertising. */
            virtual void blockAddress(UdpAddress const &addr) = 0;
            /* Return number of connected clients. This includes clients who are in a timed out state but 
               not yet shut down. Does not include clients that have been shut down. */
            virtual size_t numPlayers() = 0;
            /* Return a particular client, WITHOUT ADDING A REFCOUNT. Return null if index is out of range. */
            virtual UdpPlayer *peekPlayer(size_t index) = 0;

            /* Send a message to all players, possibly excluding one player. */
            virtual void sendToAll(void const *buf, size_t size, UdpPlayer *exclude = nullptr) = 0;
    };

    /* Implemented by the application to receive notifications about connecting and staying connected 
       to a game server. */
    class UdpGameListener : public Ref {
    public:
        /* While waiting for the first packet, or during brief communication failure, onProgress() is 
           called with the time-since-last-packet. */
        virtual void onProgress(double timeSinceStart) = 0;
        /* onConnected() is called if receiving a return packet, if not already considered connected. 
           You can then communicate with the remote endpoint using the given io interface. You will also 
           be given a player ID at this point. */
        virtual void onConnected(UdpPacketIO *io, size_t playerId) = 0;
        /* If some time elapses without hearing from the server, you will be told that you're timed out 
           from the server. The client will keep attempting to re-connect until you call disconnectFromGame()
           manually. */
        virtual void onTimeOut() = 0;
        /* onDisconnected() is called if receiving an authoritative "disconnect" message, or if timing 
           out after a very long time (something like 10 seconds.) The UdpClient will behave as if you 
           had called disconnectFromGame() from this point on.
           The io interface received in onConnected() becomes invalid before this notification (hence, the
           improtance to not mix threads!) */
        virtual void onDisconnected() = 0;
        /* another player joined the game */
        virtual void onJoined(size_t playerId, char const *name) = 0;
        /* another player left the game */
        virtual void onLeft(size_t playerId) = 0;

    protected:
        virtual ~UdpGameListener() {}
    };

    class UdpClient : public Ref {
        public:

            /* Create a UdpClient that you use to discover and connect to game sessions. */
            static UdpClient *create(char const *gameName);

            /* You must call update() each time through your game loop (or some other thread that you use 
               for all network operations.) Nothing actually happens without update(). */
            virtual void update(float elapsedTime) = 0;
            /* How much time has accumulated in update() calls? */
            virtual double accumulatedTime() = 0;

            /* Start listening for broadcast games on the local network (assuming a server is doing 
               advertising.) Fails if you are currently connected to a game. Note that only one UdpServer 
               and UdpClient can use the same port number at the same time.
               Warning: Make sure the process has UDP broadcast listening permission. On some OS-es, this 
               may not be allowed by default. */
            virtual bool startScanning(unsigned short port) = 0;
            /* Return how many games are currently visible on the network. A game is currently visible if 
               it has emitted an advertising broadcast packet in the last few seconds. */
            virtual size_t numPossibleGames() = 0;
            /* Return information about a particular game currently visible on the network. Return FALSE 
               if index out of range.*/
            virtual bool getPossibleGame(size_t index, UdpPossibleGame &ogame) = 0;
            /* Stop scanning for potential games. Previously found potentials are removed. */
            virtual void stopScanning() = 0;
            /* Return true if currently scanning. */
            virtual bool isScanning() = 0;

            /* Attempt to connect to a particular remote game. The listener will be told about 
               progress. It will keep being told about progress until you call disconnectFromGame()
               or re-call connectToGame(). You can only be connected to one game at a time. If the 
               client is currently scanning, it will stop when you call connectToGame(). This function 
               will return true when starting to connect -- it doesn't yet know whether there actually 
               is a working game server on the other end. The listener will be told about progress. 
               playerName and playerPassword must have strlen() of 31 or less. Note: passwords will be 
               clear text on the wire! */
            virtual bool connectToGame(UdpAddress const &upg, UdpGameListener *listener, char const *playerName, char const *playerPassword) = 0;
            /* Stop talking to a particular game instance. Even if the game instance has timed out, the 
               listener is still being updated until such time as you call disconnectFromGame(). */
            virtual void disconnectFromGame() = 0;
            /* As long as connectToGame() has been called and disconnectFromGame() hasn't yet been 
               called, isConnecting() will return true, else false. I e, the client may be "establishing 
               a connection" or "actually connected" or "having timed out from a previous connection" 
               while this is true. */
            virtual bool isConnecting() = 0;
            /* If you have been accepted by the server, you will have a player ID that is non-zero. */
            virtual size_t playerId() = 0;

        protected:
            virtual ~UdpClient() {}
    };

    class UdpMarshal
    {
        public:
            static bool r_int(unsigned char const *&ptr, size_t &size, size_t *oval);
            static bool w_int(unsigned char *&ptr, size_t &size, size_t val);
            static bool w_cpy(unsigned char *&ptr, size_t &size, void const *src, size_t n);
            static uint16_t r_u16(unsigned char const *buf, int offset);
            static bool w_u16(unsigned char *&buf, size_t &sz, uint16_t val);
            static uint32_t r_u32(unsigned char const *buf, int offset);
            static bool w_u32(unsigned char *&buf, size_t &sz, uint32_t val);
            static int r_nstr(unsigned char const *buf, int offset, char *obuf, size_t maxsize);
            static bool w_nstr(unsigned char *&buf, size_t &sz, size_t maxsize, char const *istr);
    };

    class UdpCommunicator
    {
        public:
            static bool init();
    };
}

#endif  //  UdpCommunicator_h
