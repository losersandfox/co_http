#include "Router.hpp"
#include "co_async/IOContext.hpp"
#define IS_ASYNC_SEVRER 1
#define IOURING_OR_EPOLL 1
#include "BytesBuffer.hpp"
#include "Error.hpp"
#include "Http.hpp"
#include "AsyncHandler.hpp"
#include "co_async/AsyncLoop.hpp"
#include "socket.hpp"
#include "co_async/Base.hpp"
#include "co_async/Epoll.hpp"

#include <chrono>
#include <cstring>
#include <fmt/base.h>
#include <memory>
#include <netdb.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <thread>
#include <iostream>


std::vector<std::thread> pool;


#if(IS_ASYNC_SEVRER == 0) 
void server(){
    int port = 8080;
    http::HttpResolver resolver("127.0.0.1", std::to_string(port));
    int listenfd = resolver.connect(); 
    // 尝试端口直到成功
    while(listenfd == -1) {
        // 利用作用域自动管理生命周期
        {
            http::HttpResolver resolver("127.0.0.1", std::to_string(port));
            listenfd = resolver.connect();
            if (listenfd != -1) {
                break; // 成功连接，跳出
            }
        } // resolver 在这里自动析构，不需要手动调用
        
        port++;
        if (port > 65535) break; // 防止死循环
    }
    spdlog::info("bind port: {}", port);
    while(true){
        http::SocketAddrStorage socketStorage;
        int connectid = CHECK_CALL(accept, listenfd, &socketStorage.sock, &socketStorage.sock_t);
        pool.emplace_back([connectid]  {
            while(true)
            {   
                spdlog::info("连接成功: {}", connectid);
                auto conn = AsyncFile::sync_wrap(connectid);
                auto id = std::this_thread::get_id();
                std::cout << id << std::endl;
                BytesBuffer buf(1024);
                http::HttpRequestParse req;
                do{
                    size_t n = conn.read_sync(buf);
                    if(n == 0) {
                        spdlog::info("关闭了连接： {}", connectid);
                        return;
                    }
                    req.push_chunck(buf);
                }while(req.body_finished());
                std::string body = req.body();
                if(body == ""){
                    body = "你的正文请求为空";
                }

                http::HttpResponseWriter<> res_writer;
                res_writer.begin_header(200);
                res_writer.write_header("Connection", "keep-alive");
                res_writer.write_header("Content-Type", "application/json;charset=utf-8");
                res_writer.write_header("Server", "co_http");
                res_writer.write_header("Content-length", body.size());
                res_writer.end_header();

                auto &buffer = res_writer.buffer();
                conn.write_sync(buffer);
                conn.write_sync(BytesBuffer(body));
                fmt::print("{}", buffer.data());
                fmt::println("{}", body);

                
            }
            close(connectid);
        });
   }
}
#else
#if(IOURING_OR_EPOLL == 0)
co_async::Task<void> async_server(){
    auto listen = co_await http::async_bind("127.0.0.1", "8080");
    if(listen.fileNo() == -1){
        spdlog::error("无法绑定端口");
        co_await co_async::wait_file_event(co_async::get_epoll_loop(), listen, EPOLLOUT);
        int err = http::socketGetOption<int>(listen, SOL_SOCKET, SO_ERROR);
    }
    listen.setNonblock();
    auto &loop = co_async::get_async_loop();
    while(true){
        co_await co_async::wait_file_event(loop, listen, EPOLLIN);
        http::SocketAddrStorage socketStorage{};
        int connectid = CHECK_CALL(accept, listen.fileNo(), &socketStorage.sock, &socketStorage.sock_t);
        if(connectid == -1){
            spdlog::error("accept error: {}", std::strerror(errno));
            continue;
        }
        spdlog::info("连接成功: {}", connectid);
        co_async::AsyncFile conn(connectid);
        conn.setNonblock();
        auto handler = std::make_unique<HttpConnectionHandler>();
        co_await handler->do_init(std::move(conn));
    }
}

co_async::Task<void> keepalive_task(){
    while(true){
        co_await co_async::sleep_for(std::chrono::seconds(5));
        spdlog::info("keepalive");
    }
}
#else
    //todo: io_uring版本的服务器
    co_async::Task<void> async_server(){
        int port = 8080;
        auto listenfd = co_await http::async_bind("127.0.0.1", std::to_string(port));
        while(listenfd.fileNo() == -1) {
            port++;
            if (port > 65535) {
                spdlog::error("无法绑定端口，端口号已达上限");
                co_return;
            }
            listenfd = co_await http::async_bind("127.0.0.1", std::to_string(port));
        }
        fmt::println("bind port: {}", port);
        auto &loop = co_async::get_io_context();
        while(true){
            http::SocketAddrStorage addr{};
            int connfd = co_await loop.accept(listenfd.fileNo(), &addr.sock, &addr.sock_t);
            if (connfd < 0) {
                spdlog::error("accept error: {}", std::strerror(connfd));
                continue;
            }
            spdlog::info("accept conn: {}", connfd);
            auto handler = std::make_unique<HttpServer>();
            handler->do_init(co_async::AsyncFile(connfd));
            co_await handler->run(connfd); // 启动一个独立协程处理该连接（不 co_await）
        }
    }
#endif
// http::get("/ping", [](http::HttpRequestParse<>& req) -> http::RouteResult {
//     return {200, "pong", "text/plain;charset=utf-8"};
// });

// http::post("/echo", [](http::HttpRequestParse<>& req) -> http::RouteResult {
//     // 注意：如果 HttpRequestParse::body() 不是 const，请使用 非 const 引用 如上；
//     // 若 Router::RouteHandler 是 const 引用签名，请把 lambda 参数改为 const http::HttpRequestParse<>&
//     return {200, req.body(), "application/json;charset=utf-8"};
// });

void server(){
    auto task = async_server();
#if(IOURING_OR_EPOLL == 0)
    auto &loop = co_async::get_async_loop();
#else
    auto &loop = co_async::get_io_context();
#endif
    co_async::run_task(loop, task);
}
#endif