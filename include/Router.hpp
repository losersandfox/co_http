#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include "Http.hpp"

namespace http {

struct RouteResult {
    int status = 200;
    std::string body;
    std::string content_type = "application/json;charset=utf-8";
};

using RouteHandler = std::function<RouteResult(const http::HttpRequestParse<>&)>;

struct Router {
    void add(HttpMethod method, std::string path, RouteHandler handler){
        routes[method][std::move(path)] = std::move(handler);
    }

    void add_get(std::string path, RouteHandler handler){ add(HttpMethod::GET, std::move(path), std::move(handler)); }
    void add_post(std::string path, RouteHandler handler){ add(HttpMethod::POST, std::move(path), std::move(handler)); }
    void add_put(std::string path, RouteHandler handler){ add(HttpMethod::PUT, std::move(path), std::move(handler)); }
    void add_delete(std::string path, RouteHandler handler){ add(HttpMethod::DELETE, std::move(path), std::move(handler)); }
    void add_patch(std::string path, RouteHandler handler){ add(HttpMethod::PATCH, std::move(path), std::move(handler)); }


    RouteResult handle(http::HttpRequestParse<>& req) const {
        // 从 headline 解析 method/url 使用已有 parser API
        // 这里假定 HttpRequestParse::method() 返回 string like "GET"
        // 将其映射到 enum HttpMethod
        std::string m = req.method();
        std::string u = req.url();
        HttpMethod hm = HttpMethod::UNKNOWN;
        if (m == "GET") hm = HttpMethod::GET;
        else if (m == "POST") hm = HttpMethod::POST;
        else if (m == "PUT") hm = HttpMethod::PUT;
        else if (m == "DELETE") hm = HttpMethod::DELETE;
        else if (m == "HEAD") hm = HttpMethod::HEAD;
        else if (m == "OPTIONS") hm = HttpMethod::OPTIONS;
        else if (m == "PATCH") hm = HttpMethod::PATCH;
        else if (m == "TRACE") hm = HttpMethod::TRACE;
        else if (m == "CONNECT") hm = HttpMethod::CONNECT;

        auto itm = routes.find(hm);
        if (itm != routes.end()) {
            auto itp = itm->second.find(u);
            if (itp != itm->second.end()) {
                return itp->second(req);
            }
        }
        return {404, std::string("Not Found"), "text/plain;charset=utf-8"};
    }

    std::map<HttpMethod, std::map<std::string, RouteHandler>> routes;
};

// 全局路由表
inline Router g_router;
inline void get(const std::string &path, RouteHandler handler){
    g_router.add_get(path, std::move(handler));
}
inline void post(const std::string &path, RouteHandler handler){
    g_router.add_post(path, std::move(handler));
}
inline void put(const std::string &path, RouteHandler handler){
    g_router.add_put(path, std::move(handler));
}
inline void delete_route(const std::string &path, RouteHandler handler){
    g_router.add_delete(path, std::move(handler));
}
inline void patch(const std::string &path, RouteHandler handler){
    g_router.add_patch(path, std::move(handler));
}

}
