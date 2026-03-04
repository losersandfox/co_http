#pragma once
#include "BytesBuffer.hpp"
#include "Http.hpp"

#include <cstring>
#include <fmt/base.h>
#include <fmt/format.h>
#include <netdb.h>
#include <spdlog/spdlog.h>
#include <vector>
#include <thread>
#include "co_async/AsyncLoop.hpp"
#include "co_async/Base.hpp"
#include "co_async/Epoll.hpp"
#include "co_async/Stream.hpp"
#include "co_async/IOContext.hpp"
#include "socket.hpp"
#include "Router.hpp"

#define IOURING_OR_EPOLL 1

//加extern防止有一个pool变量在别处定义了，这里只是引用它
extern std::vector<std::thread> pool;
extern void server();

struct HttpRequest{

};
struct HttpServer{
    co_async::AsyncFile m_conn;
    
    co_async::FileStream m_stream;
    BytesBuffer m_buf{1024};
    http::HttpRequestParse<> m_http_req_parser;

    HttpServer() = default;
    void do_init(co_async::AsyncFile &&connfd){
        m_conn = std::move(connfd);
        #if(IOURING_OR_EPOLL == 0)
        m_stream = co_async::FileStream(co_async::get_async_loop(), co_async::AsyncFile(m_conn.fileNo()));
        #else
        m_stream = co_async::FileStream(co_async::get_io_context(), co_async::AsyncFile(m_conn.fileNo()));
        #endif
    }

#if(IOURING_OR_EPOLL == 0)
   co_async::Task<void> do_read(){
        spdlog::info("异步服务，开始读取");
        while(true){
            size_t n = co_await m_stream.read(m_buf);
            if(n == 0){
                spdlog::info("关闭了连接");
                do_close();
                co_return;
            }
            m_http_req_parser.push_chunk(m_buf);
            spdlog::info("读取了 {} 字节", n);
            co_await do_write();
        }
    }

    co_async::Task<void> do_write(){
        auto body = m_http_req_parser.body();
        if (body.empty()){
            body = "正文请求为空";
        }else{
            body = fmt::format("你好，你的正文内容是{}，字节为{}Byte长", body, body.size());
        }
        
        http::HttpResponseWriter<> res_writer;
        res_writer.begin_header(200);
        res_writer.write_header("Connection", "keep-alive");
        res_writer.write_header("Content-Type", "application/json;charset=utf-8");
        res_writer.write_header("Server", "co_http");
        res_writer.write_header("Content-length", body.size());
        res_writer.end_header();

        auto &buffer = res_writer.buffer();
        co_await m_stream.write(buffer);
        co_await m_stream.write(BytesBuffer(body));
        fmt::print("{}", buffer.data());
        fmt::println("{}", body);
    }
#else
    // 实际处理协程
    co_async::Task<void> write_into(){
        std::string method = m_http_req_parser.method();
        std::string url = m_http_req_parser.url();
        std::string body = m_http_req_parser.body();
        int status = 200;
        std::string content_type = "application/json;charset=utf-8";
        std::string resp_body;

        if (method == "GET") {
            resp_body = "pong";
            content_type = "text/plain;charset=utf-8";
        } else if (method == "POST") {
            resp_body = body;
            content_type = "application/json;charset=utf-8";
        } else {
            status = 404;
            resp_body = "Not Found";
            content_type = "text/plain;charset=utf-8";
        }

        http::HttpResponseWriter<> res_writer;
        res_writer.begin_header(status);
        res_writer.write_header("Connection", "keep-alive");
        res_writer.write_header("Content-Type", content_type);
        res_writer.write_header("Server", "co_http");
        res_writer.write_header("Content-length", static_cast<int>(resp_body.size()));
        res_writer.end_header();

        auto &hdr = res_writer.buffer();
        // write header
        co_await m_stream.write(hdr);
        // write body
        if(!resp_body.empty()){
            co_await m_stream.write(resp_body);
        }
        fmt::println("{}", hdr.data());
        fmt::println("{}", resp_body);
        // 重置 parser 以便处理下一个请求（keep-alive）
        m_http_req_parser = {};
    }

    co_async::Task<void> run(int connectid){
        spdlog::info("异步服务开始启动");
        while(true){
            ssize_t n = co_await m_stream.read(m_buf);
            if(n < 0){
                spdlog::info("读取出错: {}", connectid);
                do_close();
                break;
            }
            BytesConstView view{m_buf.data(), static_cast<size_t>(n)};
            m_http_req_parser.push_chunk(const_cast<BytesConstView&>(view));
            spdlog::info("读取了 {} 字节", n);

            if(m_http_req_parser.body_finished()){
                continue; // 继续读，直到完整请求
            }

            // 生成响应（简单路由）
            std::string method = m_http_req_parser.method();
            std::string url = m_http_req_parser.url();
            std::string body = m_http_req_parser.body();
            int status = 200;
            std::string content_type = "application/json;charset=utf-8";
            std::string resp_body;

            if (method == "GET") {
                resp_body = "pong";
                content_type = "text/plain;charset=utf-8";
            } else if (method == "POST") {
                resp_body = body;
                content_type = "application/json;charset=utf-8";
            } else {
                status = 404;
                resp_body = "Not Found";
                content_type = "text/plain;charset=utf-8";
            }

            http::HttpResponseWriter<> res_writer;
            res_writer.begin_header(status);
            res_writer.write_header("Connection", "keep-alive");
            res_writer.write_header("Content-Type", content_type);
            res_writer.write_header("Server", "co_http");
            res_writer.write_header("Content-length", static_cast<int>(resp_body.size()));
            res_writer.end_header();

            auto &hdr = res_writer.buffer();
            // write header
            co_await m_stream.write(hdr);
            // write body
            if(!resp_body.empty()){
                co_await m_stream.write(resp_body);
            }
            fmt::println("{}", hdr.data());
            fmt::println("{}", resp_body);
            // 重置 parser 以便处理下一个请求（keep-alive）
            m_http_req_parser = {};
        }
    }
#endif


    void do_close(){
        close(m_conn.fileNo());
    }
};


