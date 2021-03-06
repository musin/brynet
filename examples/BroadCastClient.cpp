#include <functional>
#include <time.h>
#include <stdio.h>
#include <thread>
#include <iostream>
#include <assert.h>
#include <chrono>
#include <memory>
#include <thread>
#include <atomic>

#include <brynet/utils/packet.h>

#include <brynet/net/SocketLibFunction.h>

#include <brynet/net/EventLoop.h>
#include <brynet/net/DataSocket.h>
#include <brynet/timer/Timer.h>

using namespace std;
using namespace brynet;
using namespace brynet::net;

atomic_llong  TotalRecvPacketNum = ATOMIC_VAR_INIT(0);
atomic_llong TotalRecvSize = ATOMIC_VAR_INIT(0);

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: <server ip> <server port> <session num> <packet size>\n");
        exit(-1);
    }

    std::string ip = argv[1];
    int port = atoi(argv[2]);
    int clietNum = atoi(argv[3]);
    int packetLen = atoi(argv[4]);

    ox_socket_init();

    auto clientEventLoop = std::make_shared<EventLoop>();

    for (int i = 0; i < clietNum; i++)
    {
        int fd = ox_socket_connect(false, ip.c_str(), port);
        ox_socket_setsdsize(fd, 32 * 1024);
        ox_socket_setrdsize(fd, 32 * 1024);
        ox_socket_nodelay(fd);

        DataSocket::PTR datasSocket = new DataSocket(fd, 1024 * 1024);
        datasSocket->setEnterCallback([packetLen](DataSocket::PTR datasSocket) {
            static_assert(sizeof(datasSocket) <= sizeof(int64_t), "");

            std::shared_ptr<BigPacket> sp = std::make_shared<BigPacket>(1);
            sp->writeINT64((int64_t)datasSocket);
            sp->writeBinary(std::string(packetLen, '_'));

            for (int i = 0; i < 1; ++i)
            {
                datasSocket->send(sp->getData(), sp->getLen());
            }

            datasSocket->setDataCallback([](DataSocket::PTR datasSocket, const char* buffer, size_t len) {
                const char* parseStr = buffer;
                int totalProcLen = 0;
                size_t leftLen = len;

                while (true)
                {
                    bool flag = false;
                    if (leftLen >= PACKET_HEAD_LEN)
                    {
                        ReadPacket rp(parseStr, leftLen);
                        PACKET_LEN_TYPE packet_len = rp.readPacketLen();
                        if (leftLen >= packet_len && packet_len >= PACKET_HEAD_LEN)
                        {
                            TotalRecvSize += packet_len;
                            TotalRecvPacketNum++;

                            ReadPacket rp(parseStr, packet_len);
                            rp.readPacketLen();
                            rp.readOP();
                            int64_t addr = rp.readINT64();

                            if (addr == (int64_t)(datasSocket))
                            {
                                datasSocket->send(parseStr, packet_len);
                            }

                            totalProcLen += packet_len;
                            parseStr += packet_len;
                            leftLen -= packet_len;
                            flag = true;
                            rp.skipAll();
                        }
                        rp.skipAll();
                    }

                    if (!flag)
                    {
                        break;
                    }
                }

                return totalProcLen;
            });

            datasSocket->setDisConnectCallback([](DataSocket::PTR datasSocket) {
                delete datasSocket;
            });
        });

        clientEventLoop->pushAsyncProc([clientEventLoop, datasSocket]() {
            if (!datasSocket->onEnterEventLoop(clientEventLoop))
            {
                delete datasSocket;
            }
        });
    }

    auto now = std::chrono::steady_clock::now();
    while (true)
    {
        clientEventLoop->loop(10);
        if ((std::chrono::steady_clock::now() - now) >= std::chrono::seconds(1))
        {
            cout << "total recv:" << (TotalRecvSize / 1024) / 1024 << " M /s" << " , num " <<  TotalRecvPacketNum << endl;

            now = std::chrono::steady_clock::now();
            TotalRecvSize = 0;
            TotalRecvPacketNum = 0;
        }
    }
}
