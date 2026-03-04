#pragma once

#include "co_async/Base.hpp"
#include "co_async/Epoll.hpp"


namespace co_async{
    struct AsyncLoop {
        bool run() {
            while (true) {
                auto timeout = mTimerLoop.run();
                if (mEpollLoop.hasEvent()) {
                    return mEpollLoop.run(timeout);
                } else if (timeout) {
                    std::this_thread::sleep_for(*timeout);
                } else {
                    break;
                }
            }
            return false;
        }

        operator TimerLoop &() {
            return mTimerLoop;
        }

        operator EpollLoop &() {
            return mEpollLoop;
        }

    private:
        TimerLoop& mTimerLoop = get_timer_loop();
        EpollLoop& mEpollLoop = get_epoll_loop();
    };

    inline AsyncLoop& get_async_loop() {
        static AsyncLoop loop;
        return loop;
    }
}