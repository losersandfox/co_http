#include "AsyncHandler.hpp"
#include <fmt/base.h>
#include <system_error>

int main(){
    setlocale(LC_ALL, "zh_CN.UTF-8");
    try{
        server();
    }
    catch(std::system_error &e){
        fmt::println("错误: {}", e.what());
    }
}
