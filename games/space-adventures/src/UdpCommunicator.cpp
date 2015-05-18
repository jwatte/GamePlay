#include <gameplay.h>

#if defined(_WIN32)

#include <Winsock2.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

#define SENDTO_FLAGS 0
typedef int socklen_t;

int make_socket_nonblocking(int s)
{
    u_long v = 1;
    int r = ioctlsocket(s, FIONBIO, &v);
    return r;
}

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#define INVALID_SOCKET -1
#define SENDTO_FLAGS MSG_DONTWAIT
#define closesocket close
typedef int SOCKET;

int make_socket_nonblocking(int s)
{
    return 0;
}

#endif

#include "UdpCommunicator.h"
#include <functional>


#define MAX_QUEUED_TOTAL 65536      //  actual messages payload; something more added for framing
#define MAX_PER_PACKET_TOTAL 1200   //  individual message payload; something more added for framing
#define MIN_SEND_INTERVAL 0.010f    //  100 Hz max send rate, even if filling up the buffer entirely
#define NORMAL_SEND_INTERVAL 0.050f //  20 Hz if there is some data, but not enough to fill a packet
#define MAX_SEND_INTERVAL 0.200f    //  5 Hz keepalive rate minimum, even if no data
#define BROADCAST_INTERVAL 0.5f     //  2 Hz broadcast for discovery
#define SCAN_TIMEOUT 3.0f           //  after a server has stopped sending, expire an entry
#define NUM_SENDINFOS 8             //  how many old packets to keep data on to estimate RTT
#define DISCONNECT_TIMEOUT 3.0f     //  time without answer from server before I declare no longer connected

#define SCANNING_MAGIC 0xAA01u
#define CONNECTING_MAGIC 0xAA02u

namespace space {
    using namespace gameplay;

    struct framing
    {
        uint16_t magic;
        uint16_t seqnum;
        uint16_t yourseq;
        uint16_t yourloss;
    };
    #define FRAMING_SIZE 8

    struct sendinfo
    {
        double sendtime;
        uint16_t seqno;
        uint16_t npack;
        uint16_t size;
    };

    UdpAddress::UdpAddress()
    {
        memset(this, 0, sizeof(*this));
    }

    UdpAddress::UdpAddress(struct sockaddr_in const &sin)
    {
        memset(this, 0, sizeof(*this));
        addrSize = 4;
        memcpy(ipaddr, &sin.sin_addr, 4);
        port = ntohs(sin.sin_port);
    }

    /* todo: implement me
    UdpAddress::UdpAddress(char const *name)
    {
    }
    */

    UdpAddress UdpAddress::broadcast4(unsigned short port)
    {
        UdpAddress ret;
        ret.addrSize = 4;
        memset(ret.ipaddr, 0xff, 4);
        ret.port = port;
        return ret;
    }

    /* todo: implement me
    UdpAddress UdpAddress::broadcast6(unsigned short port)
    {
    }
    */

    bool UdpAddress::operator==(UdpAddress const &o) const
    {
        return addrSize == o.addrSize &&
            !memcmp(ipaddr, o.ipaddr, addrSize) &&
            port == o.port;
    }

    bool UdpAddress::operator!=(UdpAddress const &o) const
    {
        return !(*this == o);
    }

    bool UdpAddress::operator<(UdpAddress const &o) const
    {
        if (addrSize > o.addrSize)
        {
            return false;
        }
        if (addrSize < o.addrSize)
        {
            return true;
        }
        int cmp = memcmp(ipaddr, o.ipaddr, addrSize);
        if (cmp > 0)
        {
            return false;
        }
        if (cmp < 0)
        {
            return true;
        }
        return port < o.port;
    }

    bool UdpAddress::operator<=(UdpAddress const &o) const
    {
        return (*this == o) || (*this < o);
    }

    bool UdpAddress::operator>(UdpAddress const &o) const
    {
        return !(*this <= o);
    }

    bool UdpAddress::operator>=(UdpAddress const &o) const
    {
        return !(*this < o);
    }

    void UdpAddress::formatAddr(char *obuf)
    {
        //  7 bytes
        if (addrSize == 0)
        {
            sprintf(obuf, "<none>");
            return;
        }
        //  at most 16 bytes
        if (addrSize == 4)
        {
            sprintf(obuf, "%d.%d.%d.%d", ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
            return;
        }
        //  40 bytes
        for (int i = 0; i != 16; i += 2)
        {
            //  todo: I don't collapse zero segments to ::
            sprintf(obuf, "%s%02x%02x", (i == 0) ? "" : ":", ipaddr[i], ipaddr[i + 1]);
            obuf += (i == 0) ? 4 : 5;
        }
    }


    UdpGameParams::UdpGameParams()
    {
        memset(this, 0, sizeof(*this));
    }


    UdpPossibleGame::UdpPossibleGame()
    {
        memset(this, 0, sizeof(*this));
    }

    bool UdpPossibleGame::operator==(UdpPossibleGame const &o) const
    {
        return memcmp(this, &o, sizeof(*this)) == 0;
    }

    bool UdpPossibleGame::operator<(UdpPossibleGame const &o) const
    {
        return memcmp(this, &o, sizeof(*this)) < 0;
    }

    bool UdpPossibleGame::operator!=(UdpPossibleGame const &o) const
    {
        return !(*this == o);
    }


    UdpStatistics::UdpStatistics()
    {
        memset(this, 0, sizeof(*this));
    }

    class CUdpPacketIO;

    struct UdpMessage {
        std::vector<char> data;
    };

    class UdpMessageFilter {
        public:
            virtual bool shouldDeliver(unsigned char const *data, size_t size) = 0;
        protected:
            virtual ~UdpMessageFilter() {}
    };

    class UdpDispatcher {
        public:
            virtual CUdpPacketIO *dispatch(UdpAddress const &from, framing const &hdr, unsigned char const *data, size_t size) = 0;
        protected:
            virtual ~UdpDispatcher() {}
    };

    class PrivateFilter : public UdpMessageFilter
    {
        public:
            bool shouldDeliver(unsigned char const *data, size_t size) override
            {
                return (size > 0) && (data[0] < CMD_MIN_RESERVED_ID || data[0] > CMD_MAX_RESERVED_ID);
            }
    };

    static PrivateFilter privateFilter;

    class CUdpPacketIO : public UdpPacketIO {
        public:
            CUdpPacketIO(UdpAddress const &targetAddress, SOCKET sock, char const *gameName) :
                targetAddress_(targetAddress),
                socket_(sock),
                gameName_(gameName),
                pendingSizeIn_(0),
                pendingSizeOut_(0),
                flushed_(false),
                lastRecvSeq_(0),
                lastSendSeq_(0),
                lossToSend_(0),
                lastSendTime_(0),
                accumTime_(0),
                rtt_(0)
            {
                memset(sent_, 0, sizeof(sent_));
            }

            ~CUdpPacketIO()
            {
                //  do not close socket here, as multiple IOs share the same socket 
                //  in the case of the server.
            }

            void closeSocket()
            {
                if (socket_ != INVALID_SOCKET)
                {
                    ::closesocket(socket_);
                    socket_ = INVALID_SOCKET;
                }
            }

            UdpAddress targetAddress_;
            SOCKET socket_;
            std::string gameName_;
            UdpStatistics statistics_;
            std::list<UdpMessage> messagesIn_;
            size_t pendingSizeIn_;
            std::list<UdpMessage> messagesOut_;
            size_t pendingSizeOut_;
            bool flushed_;
            sendinfo sent_[NUM_SENDINFOS];
            uint16_t lastRecvSeq_;
            uint16_t lastSendSeq_;
            uint16_t lossToSend_;
            double lastSendTime_;
            double accumTime_;
            float rtt_;
            unsigned char tmpbuf_[MAX_PER_PACKET_TOTAL + 40];

            bool rawSend(UdpAddress const &addr, void const *data, size_t size)
            {
                GP_ASSERT(addr.addrSize == 4);
                if (addr.addrSize != 4)
                {
                    return false;
                }
                sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                sin.sin_family = AF_INET;
                sin.sin_port = htons(addr.port);
                memcpy(&sin.sin_addr, addr.ipaddr, 4);
                int n = ::sendto(socket_, (char const *)data, size, SENDTO_FLAGS, (sockaddr const *)&sin, sizeof(sin));
                return n == size;
            }

            size_t numPackets() override
            {
                return messagesIn_.size();
            }

            virtual size_t packetSize() override
            {
                if (messagesIn_.empty())
                {
                    GP_ASSERT(pendingSizeIn_ == 0);
                    return 0;
                }
                return messagesIn_.front().data.size();
            }

            virtual void const *packetData(size_t *osize = 0) override
            {
                if (messagesIn_.empty())
                {
                    GP_ASSERT(pendingSizeIn_ == 0);
                    return 0;
                }
                size_t sz = messagesIn_.front().data.size();
                if (osize)
                {
                    *osize = sz;
                }
                return sz ? &messagesIn_.front().data[0] : "";
            }

            virtual void consumeMessage() override
            {
                if (!messagesIn_.empty())
                {
                    GP_ASSERT(pendingSizeIn_ >= messagesIn_.front().data.size());
                    pendingSizeIn_ -= messagesIn_.front().data.size();
                    messagesIn_.pop_front();
                }
            }

            virtual bool sendMessage(void const *data, size_t size) override
            {
                if (size > MAX_PER_PACKET_TOTAL)
                {
                    return false;
                }
                if (socket_ == INVALID_SOCKET)
                {
                    return false;
                }
                messagesOut_.push_back(UdpMessage());
                messagesOut_.back().data.insert(messagesOut_.back().data.end(), (char const *)data, (char const *)data + size);
                pendingSizeOut_ += size;
                while (pendingSizeOut_ > MAX_QUEUED_TOTAL)
                {
                    /* trying to send too much at a time -- drop from the front */
                    GP_ASSERT(!messagesOut_.empty());
                    pendingSizeOut_ -= messagesOut_.front().data.size();
                    messagesOut_.pop_front();
                    statistics_.numPacketsLostSent += 1;
                    statistics_.numPacketsLostRecentlySent += 1;
                }
                return true;
            }

            void flushOutput() override
            {
                flushed_ = true;
            }

            virtual void getStats(UdpStatistics &stats) override
            {
                stats = statistics_;
                statistics_.numPacketsLostRecentlySent = 0;
                statistics_.numPacketsLostRecentlyReceived = 0;
            }

            /* todo: merge the three receive modes into some kind of layer of indirection. */
            bool receiveScanning(double time, UdpPossibleGame &opg)
            {
                accumTime_ = time;
                struct sockaddr_in sin;
                socklen_t len = sizeof(sin);
                int r;
                while ((r = ::recvfrom(socket_, (char *)tmpbuf_, sizeof(tmpbuf_), SENDTO_FLAGS, (struct sockaddr *)&sin, &len)) >= 0)
                {
                    if (r < FRAMING_SIZE)
                    {
                        //  ignore
                        statistics_.numPacketsIgnored++;
                        continue;
                    }
                    framing f;
                    f.magic = UdpMarshal::r_u16(tmpbuf_, 0);
                    f.seqnum = UdpMarshal::r_u16(tmpbuf_, 2);
                    f.yourseq = UdpMarshal::r_u16(tmpbuf_, 4);
                    f.yourloss = UdpMarshal::r_u16(tmpbuf_, 6);
                    if (f.magic == SCANNING_MAGIC)
                    {
                        if (r < FRAMING_SIZE + 1 + sizeof(UdpGameParams))
                        {
                            //  ignore
                            statistics_.numPacketsIgnored++;
                            continue;
                        }
                        //  demarshal new-game-offering
                        int n;
                        char gname[32];
                        if (!(n = UdpMarshal::r_nstr(&tmpbuf_[FRAMING_SIZE], 0, gname, sizeof(gname))))
                        {
                            statistics_.numPacketsIgnored++;
                            continue;
                        }
                        if (!strncmp(gname, gameName_.c_str(), sizeof(gname)))
                        {
                            statistics_.numPacketsIgnored++;
                            continue;
                        }
                        UdpPossibleGame upg;
                        memcpy(&upg.params, &tmpbuf_[n+FRAMING_SIZE], sizeof(upg.params));
                        n += sizeof(upg.params);
                        n += UdpMarshal::r_nstr(tmpbuf_, FRAMING_SIZE + n + sizeof(upg.params), upg.sessionName, sizeof(upg.sessionName));
                        if (n > r)
                        {
                            //  ignore
                            statistics_.numPacketsIgnored++;
                            continue;
                        }
                        //  insert possible game into list of possible games
                        upg.address = UdpAddress(sin);
                        opg = upg;
                        return true;
                    }
                }
                return false;
            }

            void receiveConnecting(double time, UdpMessageFilter *filter)
            {
                accumTime_ = time;
                struct sockaddr_in sin;
                socklen_t len = sizeof(sin);
                int r;
                while ((r = ::recvfrom(socket_, (char *)tmpbuf_, sizeof(tmpbuf_), SENDTO_FLAGS, (struct sockaddr *)&sin, &len)) >= 0)
                {
                    UdpAddress frm(sin);
                    if (frm != targetAddress_)
                    {
                        statistics_.numPacketsIgnored++;
                        continue;
                    }
                    if (r < FRAMING_SIZE)
                    {
                        //  ignore
                        statistics_.numPacketsIgnored++;
                        continue;
                    }
                    framing f;
                    f.magic = UdpMarshal::r_u16(tmpbuf_, 0);
                    f.seqnum = UdpMarshal::r_u16(tmpbuf_, 2);
                    f.yourseq = UdpMarshal::r_u16(tmpbuf_, 4);
                    f.yourloss = UdpMarshal::r_u16(tmpbuf_, 6);
                    if (f.magic == CONNECTING_MAGIC)
                    {
                        UdpAddress fa(sin);
                        if (fa == targetAddress_)
                        {
                            decodeIncomingPacket(f, &tmpbuf_[FRAMING_SIZE], r - FRAMING_SIZE, filter);
                        }
                        else
                        {
                            //  ignore
                            statistics_.numPacketsIgnored++;
                            continue;
                        }
                    }
                    else
                    {
                        //  ignore
                        statistics_.numPacketsIgnored++;
                        continue;
                    }
                }
            }

            void receiveDispatching(double time, UdpDispatcher *dispatch)
            {
                accumTime_ = time;
                struct sockaddr_in sin;
                socklen_t len = sizeof(sin);
                int r;
                while ((r = ::recvfrom(socket_, (char *)tmpbuf_, sizeof(tmpbuf_), SENDTO_FLAGS, (struct sockaddr *)&sin, &len)) >= 0)
                {
                    UdpAddress frm(sin);
                    if (r < FRAMING_SIZE)
                    {
                        //  ignore
                        statistics_.numPacketsIgnored++;
                        continue;
                    }
                    framing f;
                    f.magic = UdpMarshal::r_u16(tmpbuf_, 0);
                    f.seqnum = UdpMarshal::r_u16(tmpbuf_, 2);
                    f.yourseq = UdpMarshal::r_u16(tmpbuf_, 4);
                    f.yourloss = UdpMarshal::r_u16(tmpbuf_, 6);
                    if (f.magic == CONNECTING_MAGIC)
                    {
                        CUdpPacketIO *io = dispatch->dispatch(frm, f, &tmpbuf_[FRAMING_SIZE], r - FRAMING_SIZE);
                        if (dispatch)
                        {
                            io->decodeIncomingPacket(f, &tmpbuf_[FRAMING_SIZE], r - FRAMING_SIZE, &privateFilter);
                        }
                        else
                        {
                            //  ignore
                            statistics_.numPacketsIgnored++;
                            continue;
                        }
                    }
                    else
                    {
                        //  ignore
                        statistics_.numPacketsIgnored++;
                        continue;
                    }
                }
            }

            void writeOutgoingPackets()
            {
                /* figure out whether it's time to send yet -- how long since I last sent? */
                double ot = accumTime_ - lastSendTime_;
                if (flushed_)
                {
                    flushed_ = false;
                }
                else
                {
                    if (ot < MIN_SEND_INTERVAL)
                    {
                        //  not time to re-send yet
                        return;
                    }
                    if (ot < MAX_SEND_INTERVAL)
                    {
                        /* I always sent at MAX_SEND_INTERVAL */
                        if (pendingSizeOut_ == 0)
                        {
                            //  nothing to send, and not at keepalive interval time yet
                            return;
                        }
                        if (ot < NORMAL_SEND_INTERVAL)
                        {
                            if (ot < rtt_ * 0.25f && pendingSizeOut_ < MAX_PER_PACKET_TOTAL)
                            {
                                //  Only allow 4 packets on the wire, unless reached full packet size
                                return;
                            }
                            if (ot < rtt_ * 0.125f && pendingSizeOut_ < MAX_QUEUED_TOTAL / 4)
                            {
                                //  don't allow more than average 8 packets on the wire, unless buffer is getting too full
                                return;
                            }
                        }
                    }
                }

                /* construct packet header */
                framing f;
                f.magic = CONNECTING_MAGIC;
                f.seqnum = lastSendSeq_;
                f.yourloss = lossToSend_;
                f.yourseq = lastRecvSeq_;
                lossToSend_ = 0;
                unsigned char *ptr = tmpbuf_;
                size_t sz = sizeof(tmpbuf_);
                if (!UdpMarshal::w_u16(ptr, sz, f.magic) ||
                    !UdpMarshal::w_u16(ptr, sz, f.seqnum) ||
                    !UdpMarshal::w_u16(ptr, sz, f.yourseq) ||
                    !UdpMarshal::w_u16(ptr, sz, f.yourloss))
                {
                    GP_ERROR("Failure creating packet framing.");
                    return;
                }

                /* serialize each message */
                int nmsg = 0;
                while (sz > 0 && !messagesOut_.empty())
                {
                    if (2 + messagesOut_.front().data.size() > sz)
                    {
                        break;
                    }
                    size_t sza = sz;
                    if (!UdpMarshal::w_int(ptr, sz, messagesOut_.front().data.size()) ||
                        !UdpMarshal::w_cpy(ptr, sz, &messagesOut_.front().data[0], messagesOut_.front().data.size()))
                    {
                        //  packet too long; back up
                        sz = sza;
                        break;
                    }
                    messagesOut_.pop_back();
                    ++lastSendSeq_;
                    ++nmsg;
                }

                /* figure out where to send */
                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                sin.sin_family = AF_INET;
                sin.sin_port = htons(targetAddress_.port);
                memcpy(&sin.sin_addr, targetAddress_.ipaddr, 4);

                /* actually send packets */
                int r = ::sendto(socket_, (char *)tmpbuf_, ptr - tmpbuf_, SENDTO_FLAGS, (sockaddr const *)&sin, sizeof(sin));
                statistics_.numPacketsActuallySent += nmsg;
                if (r != ptr - tmpbuf_)
                {
                    GP_WARN("Error sending packet on socket.");
                    statistics_.numPacketsLostSent += nmsg;
                    statistics_.numPacketsLostRecentlySent += nmsg;
                }

                lastSendTime_ = accumTime_;
            }

            void decodeIncomingPacket(framing const &f, unsigned char const *data, size_t sz, UdpMessageFilter *filter)
            {
                for (int i = 0; i != NUM_SENDINFOS; ++i)
                {
                    if (sent_[i].seqno == f.yourseq)
                    {
                        if (sent_[i].sendtime != 0.0)
                        {
                            float diff = float(accumTime_ - sent_[i].sendtime);
                            if (diff < rtt_)
                            {
                                //  average down
                                rtt_ = rtt_ * 0.9f + diff * 0.1f;
                            }
                            else
                            {
                                //  peak follow
                                rtt_ = diff;
                            }
                            goto found;
                        }
                    }
                }
                //  I didn't find this? rtt_ must be off by a lot (or it's during start-up)
                rtt_ += MAX_SEND_INTERVAL;
                if (rtt_ > 10 * MAX_SEND_INTERVAL)
                {
                    //  apply some remotely-sane maximum value
                    rtt_ = 10 * MAX_SEND_INTERVAL;
                }
            found:
                if ((uint16_t)(f.yourseq - lastRecvSeq_) > 0x7fff)
                {
                    //  discarding this packet, as it's out-of-sequence
                    statistics_.numPacketsIgnored++;
                    statistics_.numPacketsLostReceived++;
                    statistics_.numPacketsLostRecentlyReceived++;
                    return;
                }
                lossToSend_ += (uint16_t)(f.yourseq - lastRecvSeq_);
                lastRecvSeq_ = f.yourseq;
                statistics_.numPacketsLostSent += f.yourloss;
                statistics_.numPacketsLostRecentlySent += f.yourloss;
                /* unpack messages within the bigger network packet */
                while (sz > 0)
                {
                    size_t msglen = 0;
                    if (!UdpMarshal::r_int(data, sz, &msglen))
                    {
                        GP_WARN("Mal-formatted packet received.");
                        break;
                    }
                    if (sz < msglen)
                    {
                        GP_WARN("Truncated packet received.");
                        break;
                    }
                    if (msglen > 0)
                    {
                        if (filter->shouldDeliver(data, msglen))
                        {
                            messagesIn_.push_back(UdpMessage());
                            messagesIn_.back().data.insert(messagesIn_.back().data.end(), data, data + msglen);
                            pendingSizeIn_ += msglen;
                            while (pendingSizeIn_ > MAX_QUEUED_TOTAL)
                            {
                                statistics_.numPacketsLostReceived++;
                                statistics_.numPacketsLostRecentlyReceived++;
                                GP_ASSERT(pendingSizeIn_ >= messagesIn_.front().data.size());
                                pendingSizeIn_ -= messagesIn_.front().data.size();
                                messagesIn_.pop_front();
                            }
                        }
                    }
                    data += msglen;
                    sz -= msglen;
                    statistics_.numPacketsActuallyReceived++;
                    ++lastRecvSeq_;
                }
                statistics_.lastPacketReceivedTime = accumTime_;
            }

            /* variable-length encoding of unsigned ints */

    };

    static SOCKET make_broadcast_socket(unsigned short port)
    {
        SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == INVALID_SOCKET)
        {
            return INVALID_SOCKET;
        }
        int one = 1;
        int r;
        r = make_socket_nonblocking(sock);
        if (r < 0)
        {
            ::closesocket(sock);
            return INVALID_SOCKET;
        }
        r = ::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char const *)&one, (socklen_t)sizeof(one));
        if (r < 0)
        {
            ::closesocket(sock);
            return INVALID_SOCKET;
        }
        r = ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char const *)&one, (socklen_t)sizeof(one));
        if (r < 0)
        {
            ::closesocket(sock);
            return INVALID_SOCKET;
        }
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        r = ::bind(sock, (struct sockaddr const *)&sin, (socklen_t)sizeof(sin));
        if (r < 0)
        {
            ::closesocket(sock);
            return INVALID_SOCKET;
        }
        return sock;
    }

    class CUdpClient : public UdpClient, UdpMessageFilter {
    public:
        CUdpClient(char const *gameName) :
            gameName_(gameName),
            scanning_(false),
            connecting_(false),
            isConnected_(false),
            shouldDisconnect_(false),
            accumTime_(0),
            connectStartTime_(0),
            listener_(0),
            io_(0),
            playerId_(0)
        {
        }

        std::string gameName_;
        bool scanning_;
        bool connecting_;
        bool isConnected_;
        bool shouldDisconnect_;
        std::vector<std::pair<double, UdpPossibleGame> > possibleGames_;
        double accumTime_;
        double connectStartTime_;
        UdpAddress targetAddress_;
        UdpGameListener * listener_;
        std::string playerName_;
        std::string playerPassword_;
        CUdpPacketIO *io_;
        size_t playerId_;
        std::map<size_t, std::string> players_;

        CUdpPacketIO *makeScanningSocket(unsigned short port)
        {
            SOCKET sock = make_broadcast_socket(port);
            if (sock == INVALID_SOCKET)
            {
                return NULL;
            }
            return new CUdpPacketIO(UdpAddress::broadcast4(port), sock, gameName_.c_str());
        }

        CUdpPacketIO *makeClientSocket(UdpAddress const &addr)
        {
            SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (sock == INVALID_SOCKET)
            {
                return NULL;
            }
            int one = 1;
            int r;
            r = make_socket_nonblocking(sock);
            if (r < 0)
            {
                ::closesocket(sock);
                return NULL;
            }
            return new CUdpPacketIO(addr, sock, gameName_.c_str());
        }

        void addPossibleGame(UdpPossibleGame const &upg)
        {
            bool found = false;
            for (auto &pg : possibleGames_)
            {
                if (pg.second.address == upg.address)
                {
                    pg = std::pair<double, UdpPossibleGame>(accumTime_, upg);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                possibleGames_.push_back(std::pair<double, UdpPossibleGame>(accumTime_, upg));
            }
        }

        void purgeOldPossibleGames()
        {
            for (size_t n = possibleGames_.size(); n > 0; --n)
            {
                if (possibleGames_[n - 1].first < accumTime_ - SCAN_TIMEOUT)
                {
                    possibleGames_.erase(possibleGames_.begin() + (n - 1));
                }
            }
        }

        bool shouldDeliver(unsigned char const *data, size_t size)
        {
            if (data[0] >= CMD_MIN_RESERVED_ID && data[0] < CMD_MAX_RESERVED_ID)
            {
                size_t val;
                int n = 0;
                switch (data[0])
                {
                    case CMD_S2C_CONNECTED:
                        data += 1;
                        size -= 1;
                        if ((n = UdpMarshal::r_int(data, size, &val)) > 0)
                        {
                            if (!isConnected_)
                            {
                                playerId_ = val;
                                isConnected_ = true;
                                listener_->onConnected(io_, playerId_);
                            }
                            else
                            {
                                /* the server must not re-allocate my player ID */
                                GP_ASSERT(playerId_ == val);
                            }
                        }
                        break;
                    case CMD_S2C_DISCONNECTED:
                        gotDisconnected(data, size);
                        break;
                    case CMD_S2C_PLAYERINFO:
                        gotPlayerInfo(data, size);
                        break;
                }
                return false;
            }
            return true;
        }

        void gotPlayerInfo(unsigned char const *&data, size_t &size)
        {
            char name[32];
            size_t id;
            while (size > 0)
            {
                int n = 0;
                if (!UdpMarshal::r_int(data, size, &id) ||
                    !(n = UdpMarshal::r_nstr(data, 0, name, std::min(sizeof(name), size)))) {
                    GP_WARN("Bad player info packet from server.");
                    return;
                }
                data += n;
                size -= n;
                players_[id] = name;
                listener_->onJoined(id, name);
            }
        }

        void gotDisconnected(unsigned char const *&data, size_t &size)
        {
            size_t id;
            while (size > 0)
            {
                if (!UdpMarshal::r_int(data, size, &id))
                {
                    GP_WARN("Bad player disconnect packet from server.");
                    return;
                }
                auto ptr = players_.find(id);
                if (ptr != players_.end())
                {
                    players_.erase(ptr);
                    listener_->onLeft(id);
                }
                if (id == playerId_)
                {
                    shouldDisconnect_ = true;
                }
            }
        }


        void update(float elapsedTime) override
        {
            accumTime_ += elapsedTime;

            if (scanning_)
            {
                purgeOldPossibleGames();
            }

            if (io_)
            {
                if (scanning_)
                {
                    UdpPossibleGame upg;
                    while (io_->receiveScanning(accumTime_, upg))
                    {
                        addPossibleGame(upg);
                    }
                }
                else if (connecting_)
                {
                    io_->receiveConnecting(accumTime_, this);
                }
            }

            if (connecting_)
            {
                size_t n = io_->numPackets();
                if (n)
                {
                    connectStartTime_ = accumTime_;
                }
                else
                {
                    listener_->onProgress(accumTime_ - connectStartTime_);
                    if (!isConnected_ && !io_->pendingSizeOut_)
                    {
                        std::vector<char> packet;
                        packet.insert(packet.end(), CMD_C2S_CONNECT);
                        char const *gn = gameName_.c_str();
                        packet.insert(packet.end(), gn, gn + gameName_.size() + 1);
                        char const *pn = playerName_.c_str();
                        packet.insert(packet.end(), pn, pn + playerName_.size() + 1);
                        char const *pp = playerPassword_.c_str();
                        packet.insert(packet.end(), pp, pp + playerPassword_.size() + 1);
                        io_->sendMessage(&packet[0], packet.size());
                    }
                }
                while (n > 0)
                {
                    --n;
                    size_t sz;
                    void const *ptr = io_->packetData(&sz);
                    //  todo: do something with packet
                }
                if (isConnected_ && connectStartTime_ - accumTime_ > DISCONNECT_TIMEOUT)
                {
                    listener_->onTimeOut();
                    isConnected_ = false;
                    playerId_ = 0;
                }
            }

            if (shouldDisconnect_)
            {
                shouldDisconnect_ = false;
                disconnectFromGame();
            }

            if (io_)
            {
                io_->writeOutgoingPackets();
            }
        }

        double accumulatedTime() override
        {
            return accumTime_;
        }

        bool startScanning(unsigned short port) override
        {
            disconnectFromGame();
            stopScanning();
            io_ = makeScanningSocket(port);
            return (scanning_ = (io_ != NULL));
        }

        size_t numPossibleGames() override
        {
            return possibleGames_.size();
        }

        bool getPossibleGame(size_t index, UdpPossibleGame &ogame) override
        {
            if (index >= possibleGames_.size())
            {
                return false;
            }
            ogame = possibleGames_[index].second;
            return true;
        }

        void stopScanning() override
        {
            if (scanning_)
            {
                scanning_ = false;
                possibleGames_.clear();
                if (io_)
                {
                    delete io_;
                    io_ = NULL;
                }
            }
        }

        bool isScanning() override
        {
            return scanning_;
        }


        bool connectToGame(UdpAddress const &upg, UdpGameListener *listener, char const *playerName, char const *playerPassword) override
        {
            GP_ASSERT(upg.ipaddr[0] != 0 && upg.ipaddr[0] != 0xff);
            GP_ASSERT(upg.port != 0);
            GP_ASSERT(listener);
            GP_ASSERT(playerName);
            GP_ASSERT(playerPassword);

            disconnectFromGame();
            stopScanning();

            isConnected_ = false;
            targetAddress_ = upg;
            listener_ = listener;
            playerName_ = playerName;
            playerPassword_ = playerPassword;
            connectStartTime_ = accumTime_;

            io_ = makeClientSocket(upg);
            return (connecting_ = (io_ != NULL));
        }

        void disconnectFromGame() override
        {
            if (connecting_)
            {
                connecting_ = false;
                isConnected_ = false;
                shouldDisconnect_ = false;
                players_.clear();
                playerId_ = 0;
                if (io_)
                {
                    delete io_;
                    io_ = NULL;
                }
                if (listener_)
                {
                    listener_->onDisconnected();
                    listener_ = NULL;
                }
            }
        }

        bool isConnecting() override
        {
            return connecting_;
        }

        size_t playerId() override
        {
            return playerId_;
        }

    };

    UdpClient *UdpClient::create(char const *gameName)
    {
        return new CUdpClient(gameName);
    }


    class CUdpServer;

    class CUdpPlayer : public UdpPlayer
    {
        public:
            CUdpPlayer(CUdpServer *server, char const *name, CUdpPacketIO *io, void *cookie, size_t id);
            virtual ~CUdpPlayer();
            void shutdownClient() override;
            UdpPacketIO *io() override;
            void setCookie(void *) override;
            UdpServer *server() override;

            char const *name() override { return name_.c_str(); }
            size_t playerId() override { return id_; }
            void *cookie() override { return cookie_; }

            CUdpServer *server_;
            std::string name_;
            CUdpPacketIO *io_;
            void *cookie_;
            size_t id_;
            bool shutdown_;


    };

    class CUdpServer : public UdpServer, public UdpDispatcher
    {
        public:
            CUdpServer(char const *gameName) :
                gameName_(gameName),
                accumTime_(0),
                lastAdvertiseTime_(0),
                io_(NULL),
                serving_(false),
                advertising_(false),
                port_(0),
                playerFilter_(NULL),
                peekCachePos_(-1),
                nextPlayerId_(1)
            {
            }

            std::string gameName_;
            double accumTime_;
            double lastAdvertiseTime_;
            CUdpPacketIO *io_;
            SOCKET sock_;
            bool serving_;
            bool advertising_;
            unsigned short port_;
            char sessionName_[32];
            UdpGameParams gameParams_;
            UdpPlayerFilter *playerFilter_;
            std::map<UdpAddress, CUdpPlayer *> players_;
            std::map<UdpAddress, CUdpPlayer *>::iterator peekCache_;
            int peekCachePos_;
            std::set<UdpAddress> blocked_;
            size_t nextPlayerId_;

            CUdpPacketIO *dispatch(UdpAddress const &from, framing const &hdr, unsigned char const *data, size_t size) override
            {
                if (blocked_.find(from) != blocked_.end())
                {
                    return NULL;
                }
                auto ptr = players_.find(from);
                if (ptr == players_.end())
                {
                    //  unknown source IP -- decode and see if this is a "new player" packet
                    if (data[0] == CMD_C2S_CONNECT)
                    {
                        char gameName[32];
                        char playerName[32];
                        char password[32];
                        int n = 0;
                        int r = 0;
                        if (r = UdpMarshal::r_nstr(data, n, gameName, sizeof(gameName)))
                        {
                            n += r;
                            if (!strncmp(gameName, gameName_.c_str(), sizeof(gameName)) &&
                                (r = UdpMarshal::r_nstr(data, n, playerName, sizeof(playerName))))
                            {
                                n += r;
                                if (r = UdpMarshal::r_nstr(data, n, password, sizeof(password)))
                                {
                                    n += r;
                                    playerFilter_->considerNewPlayer(playerName, password, from);
                                }
                            }
                        }
                    }
                    return NULL;
                }
                return (*ptr).second->io_;
            }

            void update(float elapsedTime) override
            {
                accumTime_ += elapsedTime;
                if (advertising_)
                {
                    if (accumTime_ - lastAdvertiseTime_ >= BROADCAST_INTERVAL)
                    {
                        lastAdvertiseTime_ = accumTime_;
                        advertiseGame();
                    }
                }
                if (serving_)
                {
                    
                }
            }

            double accumulatedTime() override
            {
                return accumTime_;
            }

            void advertiseGame()
            {
                unsigned char buf[128] = { 0 };
                unsigned char *p = buf;
                size_t sz = 128;
                UdpPossibleGame upg;
                strncpy(upg.sessionName, sessionName_, sizeof(upg.sessionName));
                upg.sessionName[sizeof(upg.sessionName) - 1];
                upg.params = gameParams_;
                if (!UdpMarshal::w_u16(p, sz, SCANNING_MAGIC) ||
                    !UdpMarshal::w_u16(p, sz, 0) ||
                    !UdpMarshal::w_u16(p, sz, 0) ||
                    !UdpMarshal::w_u16(p, sz, 0) ||
                    !UdpMarshal::w_nstr(p, sz, 32, gameName_.c_str()) ||
                    !UdpMarshal::w_cpy(p, sz, &upg.params, sizeof(upg.params)) ||
                    !UdpMarshal::w_nstr(p, sz, sizeof(upg.sessionName), upg.sessionName))
                {
                    GP_ERROR("Could not compose game advertising packet.");
                    return;
                }
                io_->rawSend(UdpAddress::broadcast4(port_), buf, sizeof(buf)-sz);
            }

            bool startServer(unsigned short port, UdpPlayerFilter *filter) override
            {
                shutdownServer();
                SOCKET sock = make_broadcast_socket(port);
                if (sock == INVALID_SOCKET)
                {
                    return false;
                }
                io_ = new CUdpPacketIO(UdpAddress::broadcast4(port), sock, gameName_.c_str());
                port_ = port;
                serving_ = true;
                return true;
            }

            bool startAdvertising(char const *sessionName, UdpGameParams const &gp) override
            {
                GP_ASSERT(serving_);
                if (!serving_)
                {
                    GP_ERROR("startAdvertising() before startServer() is incorrect.");
                    return false;
                }
                advertising_ = true;
                strncpy(sessionName_, sessionName, sizeof(sessionName_));
                sessionName_[sizeof(sessionName_)-1] = 0;
                gameParams_ = gp;
                lastAdvertiseTime_ = 0;
                return true;
            }

            void setAdvertisingParameters(UdpGameParams const &gp) override
            {
                gameParams_ = gp;
            }

            void stopAdvertising() override
            {
                advertising_ = false;
            }

            bool getPrivateGameAddress(UdpPossibleGame &ogame) override
            {
                GP_ASSERT(!"implement me");
                return false;
            }

            void shutdownServer() override
            {
                stopAdvertising();
                port_ = 0;
                if (serving_)
                {
                    serving_ = false;
                    io_->closeSocket();
                    delete io_;
                    io_ = NULL;
                    playerFilter_ = NULL;
                    for (auto &ap : players_)
                    {
                        //  This is reaching slightly too far into details, but that might be OK
                        //  because the alternative is way more code for no real benefit!
                        ap.second->io_->socket_ = INVALID_SOCKET;
                        ap.second->release();
                    }
                    players_.clear();
                    blocked_.clear();
                }
            }

            bool isServing() override
            {
                return serving_;
            }

            bool isadvertising() override
            {
                return advertising_;
            }

            UdpPlayer *addPlayer(char const *clientName, UdpAddress const &addr, void *cookie) override
            {
                auto ptr(players_.find(addr));
                if (ptr != players_.end())
                {
                    return NULL;
                }
                size_t thisPlayerId = nextPlayerId_;
                ++nextPlayerId_;
                CUdpPacketIO *io = new CUdpPacketIO(addr, io_->socket_, gameName_.c_str());
                CUdpPlayer *up = new CUdpPlayer(this, clientName, io, cookie, thisPlayerId);
                players_[addr] = up;
                peekCachePos_ = -1;
                //  make a welcome packet
                unsigned char buf[10];
                buf[0] = CMD_S2C_CONNECTED;
                unsigned char *bptr = &buf[1];
                size_t sz = 9;
                if (!UdpMarshal::w_int(bptr, sz, thisPlayerId))
                {
                    GP_ERROR("Could not write player id to connecting player.");
                    return NULL;
                }
                up->io_->sendMessage(buf, 10 - sz);
                up->io_->flushOutput();
                return up;
            }

            UdpPlayer *peekPlayer(UdpAddress const &addr) override
            {
                auto ptr(players_.find(addr));
                if (ptr != players_.end())
                {
                    return (*ptr).second;
                }
                return NULL;
            }

            void blockAddress(UdpAddress const &addr) override
            {
                blocked_.insert(addr);
            }

            size_t numPlayers() override
            {
                return players_.size();
            }

            UdpPlayer *peekPlayer(size_t index) override
            {
                if (index >= players_.size())
                {
                    return NULL;
                }
                if (peekCachePos_ > index || peekCachePos_ <= 0)
                {
                    peekCachePos_ = 0;
                    peekCache_ = players_.begin();
                }
                while (peekCachePos_ < index)
                {
                    ++peekCache_;
                    ++peekCachePos_;
                }
                //  when index == size, I already return NULL above
                assert(peekCache_ != players_.end());
                return (*peekCache_).second;
            }

            void sendToAll(void const *buf, size_t size, UdpPlayer *exclude = nullptr) override
            {
                for (auto &ptr : players_)
                {
                    if (ptr.second != exclude)
                    {
                        ptr.second->io_->sendMessage(buf, size);
                    }
                }
            }
    };

    CUdpPlayer::CUdpPlayer(CUdpServer *server, char const *name, CUdpPacketIO *io, void *cookie, size_t id) :
        server_(server),
        name_(name),
        io_(io),
        cookie_(cookie),
        id_(id),
        shutdown_(false)
    {
    }

    CUdpPlayer::~CUdpPlayer()
    {
        delete io_;
    }

    void CUdpPlayer::shutdownClient()
    {
        shutdown_ = true;
    }

    UdpPacketIO *CUdpPlayer::io()
    {
        return server_->io_;
    }

    void CUdpPlayer::setCookie(void *c)
    {
        cookie_ = c;
    }

    UdpServer *CUdpPlayer::server()
    {
        return server_;
    }

    UdpServer *UdpServer::create(char const *gameName)
    {
        return new CUdpServer(gameName);
    }


    bool UdpMarshal::r_int(unsigned char const *&ptr, size_t &size, size_t *oval)
    {
        *oval = 0;
        while (size > 0)
        {
            unsigned char b = *ptr;
            *oval = (*oval << 7) | (b & 0x7f);
            size--;
            ptr++;
            if (!(b & 0x80))
            {
                return true;
            }
        }
        //  run off the end without getting to end byte
        return false;
    }

    bool UdpMarshal::w_int(unsigned char *&ptr, size_t &size, size_t val)
    {
        //  figure out how many bytes I need to emit in big-endian order
        size_t mask = 0x7f;
        int nbytes = 1;
        while (mask < val)
        {
            mask = (mask << 7) | 0x7f;
            ++nbytes;
        }
        if (nbytes > size)
        {
            memset(ptr, 0xff, size);
            return false;
        }
        while (nbytes-- > 0)
        {
            *ptr = (val >> (7 * nbytes)) | (nbytes == 0 ? 0 : 0x80);
            ++ptr;
            --size;
        }
        return true;
    }

    bool UdpMarshal::w_cpy(unsigned char *&ptr, size_t &size, void const *src, size_t n)
    {
        if (size < n)
        {
            memset(ptr, 0xff, size);
            return false;
        }
        memcpy(ptr, src, n);
        ptr += n;
        size -= n;
        return true;
    }

    uint16_t UdpMarshal::r_u16(unsigned char const *buf, int offset)
    {
        return ((uint16_t)buf[offset] << 8) |
            (uint16_t)buf[offset + 1];
    }

    bool UdpMarshal::w_u16(unsigned char *&buf, size_t &sz, uint16_t val)
    {
        if (sz < 2)
        {
            memset(buf, 0xff, sz);
            return false;
        }
        buf[0] = (val >> 8) & 0xff;
        buf[1] = (val >> 0) & 0xff;
        buf += 2;
        sz -= 2;
        return true;
    }

    uint32_t UdpMarshal::r_u32(unsigned char const *buf, int offset)
    {
        return ((uint32_t)buf[offset] << 24) |
            ((uint32_t)buf[offset + 1] << 16) |
            ((uint32_t)buf[offset + 2] << 8) |
            (uint32_t)buf[offset + 3];
    }

    bool UdpMarshal::w_u32(unsigned char *&buf, size_t &sz, uint32_t val)
    {
        if (sz < 4)
        {
            memset(buf, 0xff, sz);
            return false;
        }
        buf[0] = (val >> 24) & 0xff;
        buf[1] = (val >> 16) & 0xff;
        buf[2] = (val >> 8) & 0xff;
        buf[3] = (val >> 0) & 0xff;
        buf += 4;
        sz -= 4;
        return true;
    }

    int UdpMarshal::r_nstr(unsigned char const *buf, int offset, char *obuf, size_t maxsize)
    {
        GP_ASSERT(maxsize > 0);
        size_t n = 0;
        while (n < maxsize)
        {
            obuf[n] = buf[offset + n];
            ++n;
            if (obuf[n - 1] == 0)
            {
                break;
            }
        }
        if (n == maxsize && obuf[n - 1])
        {
            //  not a legitimate string
            return 0;
        }
        obuf[n - 1] = 0;
        return n;
    }

    bool UdpMarshal::w_nstr(unsigned char *&buf, size_t &sz, size_t maxsize, char const *istr)
    {
        GP_ASSERT(maxsize > 0);
        size_t n = 0;
        while (n < maxsize && n < sz)
        {
            buf[n] = istr[n];
            ++n;
            if (buf[n - 1] == 0)
            {
                sz -= n;
                buf += n;
                return true;
            }
        }
        //  string too long / buffer too short
        memset(buf, 0xff, sz);
        return false;
    }

    bool UdpCommunicator::init()
    {
#if defined(_WIN32)
        WSAData wsad = { 0 };
        int n = WSAStartup(MAKEWORD(2, 2), &wsad);
        if (n)
        {
            GP_ERROR("Could not initialize WinSock.");
            return false;
        }
#endif
        return true;
    }

}

