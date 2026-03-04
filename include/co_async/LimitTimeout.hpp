#include "co_async/Base.hpp"
#include <chrono>
#include <optional>
namespace co_async{
    template <Awaitable A, class Rep, class Period>
    Task<std::optional<typename AwaitableTraits<A>::RetType>> 
    limit_timeout(TimerLoop& loop, A&& a, std::chrono::duration<Rep, Period> timeout){
        using RetType = typename AwaitableTraits<A>::RetType;
        auto timeout_task = sleep_for(timeout);
        auto main_task = a;
        auto result = co_await when_any(main_task, timeout_task);
        if (auto *ret = std::get_if<0>(&result)) {
            co_return std::move(*ret);
        } else {
            co_return std::nullopt;
        }
    }

    template <Awaitable A, class Clk, class Dur>
    Task<std::optional<typename AwaitableTraits<A>::RetType>>
    limit_timeout_until(TimerLoop& loop, A&& a, std::chrono::duration<Clk, Dur> timeout){
        using RetType = typename AwaitableTraits<A>::RetType;
        auto timeout_task = sleep_for(timeout);
        auto main_task = a;
        auto result = co_await when_any(main_task, timeout_task);
        if (auto *ret = std::get_if<0>(&result)) {
            co_return std::move(*ret);
        } else {
            co_return std::nullopt;
        }
    }
}