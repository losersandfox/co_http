#pragma once
#include "co_async/Base.hpp"
#include "co_async/Epoll.hpp"
#include <cstddef>
#include <fmt/base.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <system_error>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <Http.hpp>
#include <Error.hpp>

namespace http{
    struct SocketAddrFatptr{
        struct sockaddr* sock;
        socklen_t sock_t;
    };

    struct SocketAddrStorage{
        union{
            struct sockaddr sock;
            struct sockaddr_storage sock_storage;
        };
        socklen_t sock_t = sizeof(struct sockaddr_storage);

        operator SocketAddrFatptr(){
            return {&sock, sock_t};
        }
    };

    // template<class HeaderParser = http11_header_parser>
    // struct HttpRequestParse{
    //     std::string h_header;
    //     std::string h_body;

    // };

    struct AddrEntry{

        struct addrinfo* m_head = nullptr;

        void resolve(std::string&& name, std::string&& service){
            int res = getaddrinfo(name.c_str(), service.c_str(), NULL, &m_head);
            if(res != 0){
                spdlog::info("getaddrinfo: {}", gai_strerror(res));
                auto ec = std::error_code(res, HttpError::gai_category());
                throw std::system_error(ec, "getaddrinfo");
            }
        }

        SocketAddrFatptr getAddr() const {
            return  {m_head->ai_addr, m_head->ai_addrlen };
        }

        [[nodiscard]] bool next_m_head()  {
            m_head = m_head->ai_next;
            if(m_head == nullptr){
                return false;
            }
            return true;
        }
        
        AddrEntry(std::string&& name, std::string&& service){
            resolve(std::move(name), std::move(service));
        } 
    };

    template<class HeaderParser = Http11HeaderParse>
    class HttpResolver{
    public:

        //创建套接字
        int run_bind() const { 
            auto sockAddr = _entry->getAddr();
            auto addrInfo = _entry->m_head;

            //创建套接字->绑定套接字->监听->获取连接
            int sockfd = CHECK_CALL(socket, addrInfo->ai_family, addrInfo->ai_socktype, addrInfo->ai_protocol);
            int listenfd = bind(sockfd, sockAddr.sock, sockAddr.sock_t);
            if(listenfd == -1){
                return -1;
            }
            CHECK_CALL(listen, sockfd, SOMAXCONN);
            return sockfd;
        }

        HttpResolver(std::string name, std::string service){
            _entry = new AddrEntry(std::move(name), std::move(service));
        }
        HttpResolver(HttpResolver& that) = delete;
        HttpResolver(HttpResolver&& that) :_entry(that._entry){
            that._entry = nullptr;
        }
        ~HttpResolver(){
            if (_entry->m_head){
                freeaddrinfo(_entry->m_head);
            }
            delete _entry;
        }
    private:
        struct AddrEntry *_entry;
    };

    template<class T>
    inline T socketGetOption(co_async::AsyncFile &sock, int level, int optId) {
        T optVal;
        socklen_t optLen = sizeof(optVal);
        CHECK_CALL(getsockopt, sock.fileNo(), level, optId, (sockaddr *)&optVal, &optLen);
        return optVal;
    }

    inline co_async::Task<co_async::AsyncFile> async_bind(std::string name, std::string service){
        HttpResolver<> resolver(name, service);
        co_async::AsyncFile sock(resolver.run_bind());
        co_return sock;
    }
}
