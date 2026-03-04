#include "co_async/Generator.hpp"
#include "co_async/Stream.hpp"
#include <chrono>
#include <coroutine>
#include <functional>
#include <co_async/Base.hpp>
#include <fmt/base.h>
#include <thread>
#include <co_async/Epoll.hpp>
#include <co_async/AsyncLoop.hpp>
#include <co_async/LimitTimeout.hpp>
#include <co_async/stdio.hpp>

using namespace co_async;
using namespace std::chrono_literals;
struct ValueType{
    int num1 = 9;
    std::string num2 = "12";
};

Task<int> coroutine_sleep1(){
    fmt::println("开始睡觉1 step 1");
    co_await sleep_for(1s);
    fmt::println("继续执行1 step 2");
    co_return 0;
}

Task<int> coroutine_sleep2(){
    fmt::println("开始睡觉2 step 1");
    co_await sleep_for(2s);
    fmt::println("继续执行2 step 2");
    co_return 1;
}

Task<int> coroutine_sleep21(){
    fmt::println("开始睡觉21 step 1");
    co_await sleep_for(2s);
    fmt::println("继续执行21 step 2");
    co_await coroutine_sleep1();
    fmt::println("继续执行21 step 3");
    co_return 121;
}

Task<ValueType> coroutine_task2() {
    fmt::println("task2 step 1");
    auto thread_id = std::this_thread::get_id();
    fmt::println("当前线程id: {}", std::hash<decltype(thread_id)>{}(thread_id));
    co_await std::suspend_always{};
    fmt::println("task2 step 2");
    co_yield {12, "alfsa"};
    fmt::println("task2 step 3");
    using namespace std::chrono_literals;
    co_await sleep_for(1s);
    fmt::println("task2 step 4");
    co_return {42, "sagea"}; 
}
Task<int> coroutine_task1(){
    fmt::println("step 1");
    auto thread_id = std::this_thread::get_id();
    fmt::println("当前线程id: {}", std::hash<decltype(thread_id)>{}(thread_id));
    co_await std::suspend_always{};
    fmt::println("step 2");
    co_yield 12;
    fmt::println("step 3");
    co_await coroutine_task2();
    fmt::println("step 4");
    co_return 41;
}

Task<int> coroutine_sleep3(){
    fmt::println("开始睡觉3 step 1");
    auto t1 = coroutine_sleep1();
    auto t2 = coroutine_sleep2();
    auto t3 = coroutine_sleep21();
    auto ret = co_await when_any(t1, t2, t3);
    fmt::println("继续执行3 step 2");
    co_return 12;
}

// Generator<int> coroutine_generator(){
//     for(int i = 0; i < 5; ++i){
//         fmt::println("生成器 step {}", i);
//         co_yield i;
//     }
// }

Task<int> coroutine_when_all(){
    fmt::println("开始 when_all step 1");
    auto t1 = coroutine_sleep1();
    auto t2 = coroutine_sleep2();
    auto t3 = coroutine_sleep21();
    auto ret = co_await when_all(t1, t2, t3);
    fmt::println("继续执行 when_all step 2");
    co_return 12;
}

Task<std::string> co_read(){
    FileStream stream(get_async_loop(), async_stdin(true));
    while(true){
        fmt::println("请输入一个字符串：");
        char buf[1024] = "\0";
        auto s = co_await stream.getline();
        // if(strcmp(buf, "quit\n") == 0){
        //     co_return "退出了";
        // }
        fmt::println("你输入了：{}", s);
        
    }
}

Generator<int> coroutine_generator(){
    for(int i = 0; i < 10; ++i){
        fmt::println("生成器 step {}", i);
        co_yield i;
    }
    co_return;
}

int main(){
    auto task = co_read();
    auto& loop = get_async_loop();
    run_task(loop, task);
     // 运行生成器
}
