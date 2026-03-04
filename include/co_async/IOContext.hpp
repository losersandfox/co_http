#pragma once
#include <liburing.h>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <atomic>
#include <chrono>
#include <system_error>
#include "co_async/Base.hpp" // 使用已有的 TimerLoop
#include <fmt/format.h>

namespace co_async {

struct IOContext {
    struct Op {
        std::coroutine_handle<> handle{};
        ssize_t result{0};
    };

    IOContext(unsigned queue_depth = 1024) {
        if (io_uring_queue_init(queue_depth, &ring, 0) < 0) {
            throw std::system_error(errno, std::generic_category(), "io_uring_queue_init");
        }
    }

    ~IOContext() {
        io_uring_queue_exit(&ring);
    }

    io_uring ring;
    bool has_pending_ops() {
        // 通过检查 io_uring 的 pending completion 来判断是否有未完成的操作
        struct io_uring_cqe *cqe = nullptr;
        return io_uring_peek_cqe(&ring, &cqe) == 0 && cqe;
    }
    
    // run() 被 Base.run_task 用作事件循环驱动。
    // 返回 true 表示可能还有工作需要继续循环（保持运行）。
    bool run() {
        // 如果有定时器，计算超时；否则 wait 至少 1 completion
        std::optional<std::chrono::system_clock::duration> next_timeout = get_timer_loop().run();
        // 如果定时器存在，使用其为等待超时时间；否则等待 1 event（阻塞）
        if (next_timeout) {
            struct __kernel_timespec ts{};
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*next_timeout).count();
            if (ms < 0) ms = 0;
            ts.tv_sec = ms / 1000;
            ts.tv_nsec = (ms % 1000) * 1000000;
            // 等待至少一个完成或者超时
            io_uring_submit_and_wait(&ring, 1);
            // 我们 don't use the ts directly because submit_and_wait doesn't accept timeout portably here;
            // keep simple: submit_and_wait(1) + timer loop already handled above.
        } else {
            io_uring_submit_and_wait(&ring, 1);
        }

        // 处理所有 pending completion
        struct io_uring_cqe *cqe = nullptr;
        while (io_uring_peek_cqe(&ring, &cqe) == 0 && cqe) {
            Op *op = reinterpret_cast<Op *>(cqe->user_data);
            if (op) {
                op->result = cqe->res;
                // resume coroutine associated with this op
                if (op->handle && !op->handle.done()) {
                    auto h = op->handle;
                    // mark seen before resuming
                    io_uring_cqe_seen(&ring, cqe);
                    // resume
                    h.resume();
                    // Note: do NOT delete op here; await_resume will delete it.
                    cqe = nullptr;
                    // peek next
                    continue;
                }
            }
            io_uring_cqe_seen(&ring, cqe);
            cqe = nullptr;
        }
        // 永远返回 true，保持 loop 活跃（由 keepalive/任务决定退出）
        return true;
    }

    // Accept awaiter: caller provides address pointers that must remain valid across suspension (coroutine frame is safe).
    struct AcceptAwaiter {
        IOContext &ctx;
        int listen_fd;
        struct sockaddr *addr;
        socklen_t *addrlen;
        Op *op = nullptr;

        AcceptAwaiter(IOContext &c, int l, struct sockaddr *a, socklen_t *alen)
            : ctx(c), listen_fd(l), addr(a), addrlen(alen) {}

        bool await_ready() const noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            op = new Op{};
            op->handle = h;
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx.ring);
            if (!sqe) {
                delete op;
                // cannot get sqe: resume immediately with error
                h.resume();
                return false;
            }
            io_uring_prep_accept(sqe, listen_fd, addr, addrlen, 0);
            sqe->user_data = reinterpret_cast<uint64_t>(op);
            io_uring_submit(&ctx.ring);
            return true;
        }
        int await_resume() noexcept {
            int res = static_cast<int>(op->result);
            delete op;
            return res;
        }
    };

    // Read awaiter: buffer pointer must remain valid across suspension (coroutine frame)
    struct ReadAwaiter {
        IOContext &ctx;
        int fd;
        void *buf;
        size_t len;
        off_t offset; // for files; for sockets usually 0 / -1
        Op *op = nullptr;

        ReadAwaiter(IOContext &c, int f, void *b, size_t l, off_t off = 0)
            : ctx(c), fd(f), buf(b), len(l), offset(off) {}

        bool await_ready() const noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            op = new Op{};
            op->handle = h;
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx.ring);
            if (!sqe) {
                delete op;
                h.resume();
                return false;
            }
            io_uring_prep_recv(sqe, fd, buf, (unsigned)len, 0);
            sqe->user_data = reinterpret_cast<uint64_t>(op);
            io_uring_submit(&ctx.ring);
            return true;
        }
        ssize_t await_resume() noexcept {
            ssize_t res = op->result;
            delete op;
            return res;
        }
    };

    // Write awaiter
    struct WriteAwaiter {
        IOContext &ctx;
        int fd;
        const void *buf;
        size_t len;
        Op *op = nullptr;

        WriteAwaiter(IOContext &c, int f, const void *b, size_t l)
            : ctx(c), fd(f), buf(b), len(l) {}

        bool await_ready() const noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            op = new Op{};
            op->handle = h;
            struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx.ring);
            if (!sqe) {
                delete op;
                h.resume();
                return false;
            }
            io_uring_prep_send(sqe, fd, buf, (unsigned)len, 0);
            sqe->user_data = reinterpret_cast<uint64_t>(op);
            io_uring_submit(&ctx.ring);
            return true;
        }
        ssize_t await_resume() noexcept {
            ssize_t res = op->result;
            delete op;
            return res;
        }
    };

    // helpers to create awaiters
    AcceptAwaiter accept(int listen_fd, struct sockaddr *addr, socklen_t *addrlen) {
        return AcceptAwaiter{*this, listen_fd, addr, addrlen};
    }

    ReadAwaiter read_some(int fd, void *buf, size_t len) {
        return ReadAwaiter{*this, fd, buf, len, 0};
    }

    WriteAwaiter write_some(int fd, const void *buf, size_t len) {
        return WriteAwaiter{*this, fd, buf, len};
    }
};

// 取得单例 IOContext（用于你的协程体系）
inline IOContext &get_io_context() {
    static IOContext ctx(1024);
    return ctx;
}

} // namespace co_async