#pragma once
#include <system_error>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cerrno>
#include <netdb.h>
#include <source_location>

namespace HttpError{
    template<int Except = 0, class T>
    static T check_error(const char* msg, T res){
        if(res == -1){
            if constexpr (Except != 0){
                if(errno == Except){
                    return -1;
                }
            }
            spdlog::error("{}: {}", msg, strerror(errno));
            auto ec = std::error_code(errno, std::system_category());
            throw std::system_error(ec, msg);
        }
        return res;
    }

    inline std::error_category const &gai_category(){
        static struct gai_category final : std::error_category{
            char const *name() const noexcept override{
                return "getaddrinfo";
            }

            std::string message(int err) const override{
                return gai_strerror(err);
            }
        }instance;
        return instance;
    }
}
auto checkErrorNonBlock(auto res, int blockres = 0, int blockerr = EWOULDBLOCK, std::source_location const &loc =
                                         std::source_location::current()) {
    if (res == -1) {
        if (errno != blockerr) [[unlikely]] {
            throw std::system_error(errno, std::system_category(),
                                    (std::string)loc.file_name() + ":" +
                                        std::to_string(loc.line()));
        }
        res = blockres;
    }
    return res;
}
#define CHECK_CALL(func, ...) HttpError::check_error(#func, func(__VA_ARGS__))
#define CHECK_CALL_EXCEPT(except, func, ...) HttpError::check_error<except>(#func, func(__VA_ARGS__))