#ifndef BRYNET_NET_EVENTLOOP_H_
#define BRYNET_NET_EVENTLOOP_H_

#include <cstdint>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>

#include <brynet/net/CurrentThread.h>
#include <brynet/net/SocketLibFunction.h>
#include <brynet/timer/Timer.h>
#include <brynet/utils/NonCopyable.h>

namespace brynet
{
    namespace net
    {
        class Channel;
        class DataSocket;
        class WakeupChannel;

        class EventLoop : public NonCopyable
        {
        public:
            typedef std::shared_ptr<EventLoop>          PTR;
            typedef std::function<void(void)>           USER_PROC;

#ifdef PLATFORM_WINDOWS
            enum class OLV_VALUE
            {
                OVL_NONE = 0,
                OVL_RECV,
                OVL_SEND,
            };

            struct ovl_ext_s
            {
                OVERLAPPED  base;
                const EventLoop::OLV_VALUE  OP;

                ovl_ext_s(OLV_VALUE op) : OP(op)
                {
                    memset(&base, 0, sizeof(base));
                }
            };
#endif

        public:
            EventLoop() noexcept;
            virtual ~EventLoop() noexcept;

            /*  timeout单位为毫秒    */
            void                            loop(int64_t milliseconds);

            bool                            wakeup();

            /*  投递一个异步回调，在EventLoop::loop被唤醒后执行 */
            void                            pushAsyncProc(USER_PROC f);

            /*  (网络线程中调用才会成功)投递回调放置在单次loop结尾时执行   */
            void                            pushAfterLoopProc(USER_PROC f);

            /*  非网络线程调用时返回nullptr   */
            TimerMgr::PTR                   getTimerMgr();

            inline bool                     isInLoopThread() const
            {
                return mSelfThreadID == CurrentThread::tid();
            }

        private:
            void                            reallocEventSize(size_t size);
            void                            processAfterLoopProcs();
            void                            processAsyncProcs();

#ifndef PLATFORM_WINDOWS
            int                             getEpollHandle() const;
#endif
            bool                            linkChannel(sock fd, Channel* ptr);
            void                            tryInitThreadID();

        private:

#ifdef PLATFORM_WINDOWS
            OVERLAPPED_ENTRY*               mEventEntries;

            typedef BOOL(WINAPI *sGetQueuedCompletionStatusEx) (HANDLE, LPOVERLAPPED_ENTRY, ULONG, PULONG, DWORD, BOOL);
            sGetQueuedCompletionStatusEx    mPGetQueuedCompletionStatusEx;
            HANDLE                          mIOCP;
#else
            epoll_event*                    mEventEntries;
            int                             mEpollFd;
#endif
            size_t                          mEventEntriesNum;
            std::unique_ptr<WakeupChannel>  mWakeupChannel;

            std::atomic_bool                mIsInBlock;
            std::atomic_bool                mIsAlreadyPostWakeup;

            std::mutex                      mAsyncProcsMutex;
            /*  投递到此eventloop的异步function队列    */
            std::vector<USER_PROC>          mAsyncProcs;                
            std::vector<USER_PROC>          mCopyAsyncProcs;

            /*  eventloop每次循环的末尾要执行的一系列函数   */
            std::vector<USER_PROC>          mAfterLoopProcs; 
            /*  用于在loop中代替mAfterLoopProcs进行遍历，避免遍历途中又添加新元素  */
            std::vector<USER_PROC>          mCopyAfterLoopProcs;        

            /*  调用loop函数所在thread的id */
            std::once_flag                  mOnceInitThreadID;
            CurrentThread::THREAD_ID_TYPE   mSelfThreadID;             

            TimerMgr::PTR                   mTimer;

            friend class DataSocket;
        };
    }
}

#endif