#pragma once

#include <chrono>
#include <co_async/Base.hpp>
#include <Error.hpp>
#include <coroutine>
#include <cstdint>
#include <fmt/base.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <coroutine>
#include <chrono>
#include <cstdint>
#include <utility>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include "co_async/Base.hpp"
#include "Error.hpp"

namespace co_async {

using EpollEventMask = std::uint32_t;

struct EpollFilePromise : Promise<EpollEventMask> {
    auto get_return_object() {
        return std::coroutine_handle<EpollFilePromise>::from_promise(*this);
    }

    EpollFilePromise &operator=(EpollFilePromise &&) = delete;

    inline ~EpollFilePromise();

    struct EpollFileAwaiter *mAwaiter{};
};

struct EpollLoop {
    inline bool addListener(EpollFilePromise &promise, int ctl);
    inline void removeListener(int fileNo);
    inline bool run(std::optional<std::chrono::system_clock::duration> timeout =
                        std::nullopt);

    bool hasEvent() const noexcept {
        return mCount != 0;
    }

    EpollLoop &operator=(EpollLoop &&) = delete;

    ~EpollLoop() {
        close(mEpoll);
    }

    int mEpoll = CHECK_CALL(epoll_create1, 0);
    std::size_t mCount = 0;
    struct epoll_event mEventBuf[64];
    std::vector<std::coroutine_handle<>> mQueue;
};

inline EpollLoop &get_epoll_loop(){
    static EpollLoop loop;
    return loop;
}

struct EpollFileAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<EpollFilePromise> coroutine) {
        auto &promise = coroutine.promise();
        promise.mAwaiter = this;
        if (!mLoop.addListener(promise, mCtlCode)) {
            promise.mAwaiter = nullptr;
            coroutine.resume();
        }
    }

    EpollEventMask await_resume() const noexcept {
        return mResumeEvents;
    }

    EpollLoop &mLoop;
    int mFileNo;
    EpollEventMask mEvents;
    EpollEventMask mResumeEvents;
    int mCtlCode = EPOLL_CTL_ADD;
};

EpollFilePromise::~EpollFilePromise() {
    if (mAwaiter) {
        mAwaiter->mLoop.removeListener(mAwaiter->mFileNo);
    }
}

bool EpollLoop::addListener(EpollFilePromise &promise, int ctl) {
    struct epoll_event event;
    event.events = promise.mAwaiter->mEvents;
    event.data.ptr = &promise;
    int res = epoll_ctl(mEpoll, ctl, promise.mAwaiter->mFileNo, &event);
    if (res == -1)
        return false;
    if (ctl == EPOLL_CTL_ADD)
        ++mCount;
    return true;
}

void EpollLoop::removeListener(int fileNo) {
    CHECK_CALL(epoll_ctl, mEpoll, EPOLL_CTL_DEL, fileNo, NULL);
    --mCount;
}

bool EpollLoop::run(
    std::optional<std::chrono::system_clock::duration> timeout) {
    while (!mQueue.empty()) {
        auto task = mQueue.back();
        mQueue.pop_back();
        task.resume();
    }
    if (mCount == 0) {
        return false;
    }
    int timeoutInMs = -1;
    if (timeout) {
        timeoutInMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(*timeout)
                .count();
    }
    int res = CHECK_CALL(epoll_wait, mEpoll, mEventBuf, std::size(mEventBuf), timeoutInMs);
    for (int i = 0; i < res; i++) {
        auto &event = mEventBuf[i];
        auto &promise = *(EpollFilePromise *)event.data.ptr;
        promise.mAwaiter->mResumeEvents = event.events;
    }
    for (int i = 0; i < res; i++) {
        auto &event = mEventBuf[i];
        auto &promise = *(EpollFilePromise *)event.data.ptr;
        std::coroutine_handle<EpollFilePromise>::from_promise(promise).resume();
    }
    return true;
}

struct [[nodiscard]] AsyncFile {
    AsyncFile() : mFileNo(-1) {}

    explicit AsyncFile(int fileNo) noexcept : mFileNo(fileNo) {}

    AsyncFile(AsyncFile &&that) noexcept : mFileNo(that.mFileNo) {
        that.mFileNo = -1;
    }

    AsyncFile &operator=(AsyncFile &&that) noexcept {
        std::swap(mFileNo, that.mFileNo);
        return *this;
    }

    ~AsyncFile() {
        if (mFileNo != -1)
            close(mFileNo);
    }

    int fileNo() const noexcept {
        return mFileNo;
    }

    int releaseOwnership() noexcept {
        int ret = mFileNo;
        mFileNo = -1;
        return ret;
    }

    void setNonblock() const {
        int attr = 1;
        CHECK_CALL(ioctl, fileNo(), FIONBIO, &attr);
    }

private:
    int mFileNo;
};

inline Task<EpollEventMask, EpollFilePromise>
wait_file_event(EpollLoop &loop, AsyncFile &file, EpollEventMask events) {
    co_return co_await EpollFileAwaiter(loop, file.fileNo(), events);
}

inline std::size_t readFileSync(AsyncFile &file, std::span<char> buffer) {
    return checkErrorNonBlock(
        read(file.fileNo(), buffer.data(), buffer.size()));
}

inline std::size_t writeFileSync(AsyncFile &file,
                                 std::span<char const> buffer) {
    return checkErrorNonBlock(
        write(file.fileNo(), buffer.data(), buffer.size()));
}

inline Task<std::size_t> read_file(EpollLoop &loop, AsyncFile &file,
                                   std::span<char> buffer) {
    co_await wait_file_event(loop, file, EPOLLIN | EPOLLRDHUP);
    auto len = readFileSync(file, buffer);
    co_return len;
}

inline Task<std::size_t> write_file(EpollLoop &loop, AsyncFile &file,
                                    std::span<char const> buffer) {
    co_await wait_file_event(loop, file, EPOLLOUT | EPOLLHUP);
    auto len = writeFileSync(file, buffer);
    co_return len;
}

} // namespace co_async

// #include <vector>

// namespace co_async {
//     struct EpollFilePromise : Promise<void>{
//         auto get_return_object(){
//             return std::coroutine_handle<EpollFilePromise>::from_promise(*this);
//         }
        
//         // 默认构造函数
//         EpollFilePromise() = default;
//         inline ~EpollFilePromise();

//         EpollFilePromise operator=(EpollFilePromise &&) = delete;
//         int m_file_no;
//         uint32_t m_events;
//         struct EpollFileAwaiter *mAwaiter{};
//     };

//     struct EpollLoop {
//         inline bool add_listener(EpollFilePromise& promise);

//         void add_task(std::coroutine_handle<> task){
//             m_queue.push_back(task);
//         }

//         void remove_listener(int file_no){
//             CHECK_CALL(epoll_ctl, m_epoll_fd, EPOLL_CTL_DEL, file_no, nullptr);
//             --count;
//         }

//         bool has_listener() const {
//             return count > 0;
//         }

//         inline bool run(std::optional<std::chrono::system_clock::duration> timeout);


//         EpollLoop &operator=(EpollLoop &&) = delete;
//         ~EpollLoop(){
//             close(m_epoll_fd);
//         }
//         int m_epoll_fd = CHECK_CALL(epoll_create1, 0);
//         struct epoll_event m_events[64];
//         std::size_t count = 0;
//         std::vector<std::coroutine_handle<>> m_queue;
//         EpollLoop() = default;
//         EpollLoop(int fd){
//             m_epoll_fd = CHECK_CALL(epoll_create1, fd);
//         }
//     };

//     inline EpollLoop& get_epoll_loop(){
//         static EpollLoop loop;
//         return loop;
//     }
//     inline EpollFilePromise::~EpollFilePromise(){
//         // 析构时从epoll实例中移除监听
//         EpollLoop& loop = get_epoll_loop();
//         fmt::println("remove listener for file {}", m_file_no);
//         loop.remove_listener(this->m_file_no);
//     }

//     struct EpollFileAwaiter{

//         bool await_ready() const noexcept { return false; }

//         void await_suspend(std::coroutine_handle<EpollFilePromise> h) noexcept {
//             auto &promise = h.promise();
//             promise.mAwaiter = this;
//             promise.m_file_no = m_file_no;
//             promise.m_events = m_events;
//             fmt::println("add listener for file {}, events: {}", m_file_no, m_events);
//             if (!m_loop.add_listener(promise)) {
//                 promise.mAwaiter = nullptr;
//                 h.resume();
//             }
//         }

//         EpollFileAwaiter(EpollLoop& loop, int file_no, uint32_t events)
//             : m_loop(loop), m_file_no(file_no), m_events(events) {}
//         void await_resume() noexcept {}

//         EpollLoop& m_loop;
//         int m_file_no;
//         uint32_t m_events;
//         uint32_t m_resume_events;
//     };

//     bool EpollLoop::add_listener(EpollFilePromise& promise){
//         struct epoll_event ev;
//         ev.events = promise.m_events | EPOLLONESHOT;
//         ev.data.ptr = &promise;
//         int ret = CHECK_CALL(epoll_ctl, m_epoll_fd, EPOLL_CTL_ADD, promise.m_file_no, &ev);
//         if(ret == -1){
//             return false;
//         }
//         ++count;
//         return true;
//     }

//     bool EpollLoop::run(
//         std::optional<std::chrono::system_clock::duration> timeout) {
//         while (!m_queue.empty()) {
//             auto task = m_queue.back();
//             m_queue.pop_back();
//             task.resume();
//         }
//         if (count == 0) {
//             return false;
//         }
//         int timeoutInMs = -1;
//         if (timeout) {
//             timeoutInMs =
//                 std::chrono::duration_cast<std::chrono::milliseconds>(*timeout)
//                     .count();
//         }
//         int res = CHECK_CALL(epoll_wait, m_epoll_fd, m_events, std::size(m_events), timeoutInMs);
//         for (int i = 0; i < res; i++) {
//             auto &event = m_events[i];
//             auto &promise = *(EpollFilePromise *)event.data.ptr;
//             promise.mAwaiter->m_resume_events = event.events;
//         }
//         for (int i = 0; i < res; i++) {
//             auto &event = m_events[i];
//             auto &promise = *(EpollFilePromise *)event.data.ptr;
//             std::coroutine_handle<EpollFilePromise>::from_promise(promise).resume();
//         }
//         return true;
//     }


//     struct [[nodiscard]] AsyncFile{
//         AsyncFile() 
//             : m_file_no(-1) {}
//         explicit AsyncFile(int file_no) 
//             : m_file_no(file_no) {}
//         AsyncFile& operator=(AsyncFile&& other) noexcept{
//             std::swap(m_file_no, other.m_file_no);
//             return *this;
//         }
//         AsyncFile(AsyncFile&& other) noexcept
//             : m_file_no(other.m_file_no) {
//             other.m_file_no = -1;
//         }
//         ~AsyncFile() {
//             if(m_file_no != -1){
//                 close(m_file_no);
//             }
//         }

//         int fileNo() const {
//             return m_file_no;
//         }
//         void setNonBlock() const{
//             int attr = 1;
//             CHECK_CALL(ioctl, m_file_no, FIONBIO, &attr);
//         }
//     private:
//         int m_file_no;
//     };

//     inline co_async::Task<void, EpollFilePromise> wait_file(EpollLoop& loop, int file_no, uint32_t events){
//         co_await EpollFileAwaiter{loop, file_no, events};
//     }

//     inline std::size_t readFileSync(AsyncFile &file, std::span<char> &buffer) {
//         return checkErrorNonBlock(
//             read(file.fileNo(), buffer.data(), buffer.size()));
//     }

//     inline std::size_t writeFileSync(AsyncFile &file,
//                                     std::span<char const> buffer) {
//         return checkErrorNonBlock(
//             write(file.fileNo(), buffer.data(), buffer.size()));
//     }

//     inline Task<std::size_t> read_file(EpollLoop &loop, AsyncFile &file,
//                                     std::span<char> &buffer) {
//         co_await wait_file(loop, file.fileNo(), EPOLLIN | EPOLLHUP);
//         auto len = readFileSync(file, buffer);
//         co_return len;
//     }

//     inline Task<std::size_t> write_file(EpollLoop &loop, AsyncFile &file,
//                                         std::span<char const> buffer) {
//         co_await wait_file(loop, file.fileNo(), EPOLLOUT | EPOLLHUP);
//         auto len = writeFileSync(file, buffer);
//         co_return len;
//     }

// }