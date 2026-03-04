#pragma once
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <co_async/RbTree.hpp>
#include <exception>
#include <fmt/base.h>
#include <functional>
#include <optional>
#include <type_traits>
#include <variant>
namespace co_async{
    template <class T = void> struct NonVoidHelper {
        using Type = T;
    };

    template <> struct NonVoidHelper<void> {
        using Type = NonVoidHelper;

        explicit NonVoidHelper() = default;
    };

    template <class T> struct Uninitialized {
        union {
            T mValue;
        };

        Uninitialized() noexcept {}
        Uninitialized(Uninitialized &&) = delete;
        ~Uninitialized() noexcept {}

        T moveValue() {
            T ret(std::move(mValue));
            mValue.~T();
            return ret;
        }

        template <class... Ts> void putValue(Ts &&...args) {
            new (std::addressof(mValue)) T(std::forward<Ts>(args)...);
        }
    };

    template <> struct Uninitialized<void> {
        auto moveValue() {
            return NonVoidHelper<>{};
        }

        void putValue(NonVoidHelper<>) {}
    };

    template <class T> struct Uninitialized<T const> : Uninitialized<T> {};

    template <class T>
    struct Uninitialized<T &> : Uninitialized<std::reference_wrapper<T>> {};

    template <class T> struct Uninitialized<T &&> : Uninitialized<T> {};

    template <class A>
    concept Awaiter = requires(A a, std::coroutine_handle<> h) {
        { a.await_ready() };
        { a.await_suspend(h) };
        { a.await_resume() };
    };

    template <class A>
    concept Awaitable = Awaiter<A> || requires(A a) {
        { a.operator co_await() } -> Awaiter;
    };

    template <class A> struct AwaitableTraits;

    template <Awaiter A> struct AwaitableTraits<A> {
        using RetType = decltype(std::declval<A>().await_resume());
        using NonVoidRetType = NonVoidHelper<RetType>::Type;
    };

    template <class A>
        requires(!Awaiter<A> && Awaitable<A>)
    struct AwaitableTraits<A>
        : AwaitableTraits<decltype(std::declval<A>().operator co_await())> {};
    

    //用来实现协程挂起恢复
    struct PreviousAwaiter{
        PreviousAwaiter(std::coroutine_handle<> prev = {}) : m_previous(prev) {}

        bool await_ready() const noexcept {
            return false;
        }
        //参数是类型擦除
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> /*h*/) noexcept {
            if(m_previous)
                return m_previous;
            else
                return std::noop_coroutine();        
        }
        void await_resume() noexcept {}
        std::coroutine_handle<> m_previous;
    };

    template <class T>
    struct Promise{
        auto get_return_object(){
            //fmt::println("创建了task对象");
            return std::coroutine_handle<Promise>::from_promise(*this);
        }
        auto initial_suspend() { return std::suspend_always{}; }
        auto final_suspend() noexcept {
            return PreviousAwaiter{m_previous};
        }
        void unhandled_exception(){
            m_exception = std::current_exception();
        }
        void return_value(T ret){
            new (&m_value) T(std::move(ret));
        }
        auto yield_value(T ret){

            new (&m_value) T(std::move(ret));
            return std::suspend_always{};
        }

        T result() {
            if (m_exception) [[unlikely]] {
                std::rethrow_exception(m_exception);
            }
            T ret = std::move(m_value);
            m_value.~T();
            return ret;
        }
        Promise() noexcept {};
        ~Promise() {};
        Promise(Promise&&) = delete;
        std::exception_ptr m_exception{};
        std::coroutine_handle<> m_previous{}; /*协程A co_await 协程B时，A暂停，控制权给B
                                                B为了知晓回到哪里，那就需要一个m_previous句柄来找到协程A
                                                （协程任务之间形成双向链表了说是）*/ 
        //结构体内用union可以防止创建对象时初始化变量
        union{
            T m_value;
        };
    };

    template <> 
    struct Promise<void>{
        auto get_return_object(){
            //fmt::println("创建了task对象");
            return std::coroutine_handle<Promise>::from_promise(*this);
        }
        auto initial_suspend() { return std::suspend_always{}; }
        auto final_suspend() noexcept { 
            return PreviousAwaiter{m_previous}; 
        }
        void unhandled_exception(){ 
            m_exception = std::current_exception();
        }
        void return_void(){}
        void result(){
            if(m_exception) [[unlikely]] {
                std::rethrow_exception(m_exception);
            }
        }
        Promise() = default;
        ~Promise() = default;
        Promise(Promise &&) = delete;

        std::coroutine_handle<> m_previous{};
        std::exception_ptr m_exception{};
    };

    template <class T = void, class P = Promise<T>>
    struct [[nodiscard("no co_await")]] Task {
        using promise_type = P;
        using handle_type = std::coroutine_handle<promise_type>;
        
        handle_type coro_handle; // 持有句柄

        Task(handle_type h) : coro_handle(h) {
        
        }
        ~Task() { if(coro_handle && coro_handle.done()) coro_handle.destroy(); } // RAII 自动销毁

        struct Awaiter{
            Awaiter(std::coroutine_handle<promise_type> h) : coro_handle(h) {}
            bool await_ready() const noexcept {return false;}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept{
                coro_handle.promise().m_previous = h;
                return coro_handle;
            }
            // do not resume here: the awaited coroutine will resume the awaiting one via m_previous
            T await_resume() {
                return coro_handle.promise().result();
            }

            std::coroutine_handle<promise_type> coro_handle;
        };
        auto operator co_await() const noexcept{
            return Awaiter{coro_handle};
        }

        operator std::coroutine_handle<>() const noexcept {
            return coro_handle;
        }
    };

    struct SleepUntilPromise : RbTree<SleepUntilPromise>::RbNode, Promise<void>{
        std::chrono::system_clock::time_point m_expire_time;
        auto get_return_object(){
            return Task<void, SleepUntilPromise>{std::coroutine_handle<SleepUntilPromise>::from_promise(*this)};
        }

        SleepUntilPromise operator=(const SleepUntilPromise&&) = delete;

        friend bool operator<(const SleepUntilPromise &a, const SleepUntilPromise &b){
            return a.m_expire_time < b.m_expire_time;
        }
    };

    // //调度器
    // struct Loop{
    //     std::queue<std::coroutine_handle<>> m_ready_queue;
    //     std::queue<std::coroutine_handle<>> m_waiting_queue;
    //     struct TimerEntry{
    //         std::chrono::system_clock::time_point expire_time;
    //         std::coroutine_handle<> coro_handle;

    //         bool operator<(TimerEntry const& that) const {
    //             return expire_time > that.expire_time;
    //         }
    //     };
    //     std::priority_queue<TimerEntry> timer_table;

    //     void add_task(std::coroutine_handle<> task){
    //         m_ready_queue.push(task);
    //     }
    //     void add_timer(std::chrono::system_clock::time_point expireTime, std::coroutine_handle<> t){
    //         timer_table.push({expireTime, t});          
    //     }
    //     void run_all(){
    //         while(!timer_table.empty() || !m_ready_queue.empty()){
    //             if(!m_ready_queue.empty()){
    //                 auto readyTask = m_ready_queue.front();
    //                 m_ready_queue.pop();
    //                 if(!m_ready_queue.empty()){
    //                     continue;
    //                 }
    //                 readyTask.resume();
    //             }
    //             if(!timer_table.empty()){
    //                 auto nowTime = std::chrono::system_clock::now();
    //                 TimerEntry timer = timer_table.top();
    //                 if(timer.expire_time < nowTime){
    //                     timer_table.pop();
    //                     timer.coro_handle.resume();
    //                 }else{
    //                     if(m_ready_queue.empty())
    //                         std::this_thread::sleep_until(timer.expire_time);
    //                 }
    //             }
    //         }
    //     }
    // };
    struct TimerLoop{
        RbTree<SleepUntilPromise> m_timer_tree;

        void add_timer(SleepUntilPromise &promise){
            m_timer_tree.insert(promise);          
        }

        bool has_event(){
            return !m_timer_tree.empty();
        }

        std::optional<std::chrono::system_clock::duration> run() {

            while (!m_timer_tree.empty()) {
                auto nowTime = std::chrono::system_clock::now();
                auto &promise = m_timer_tree.front();
                if (promise.m_expire_time < nowTime) {
                    m_timer_tree.erase(promise);
                    std::coroutine_handle<SleepUntilPromise>::from_promise(promise)
                        .resume();
                } else {
                    return promise.m_expire_time - nowTime;
                }
            }
            return std::nullopt;
        }
    };

    inline TimerLoop &get_timer_loop(){
        static TimerLoop loop;
        return loop; 
    }


    struct SleepAwaiter{
        bool await_ready() const noexcept{
            return false;
        }
        
        void await_suspend(std::coroutine_handle<SleepUntilPromise> h) const noexcept {
            fmt::println("协程挂起，等待时间: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(m_expire_time - std::chrono::system_clock::now()).count());
            auto &promise = h.promise();
            promise.m_expire_time = m_expire_time;
            get_timer_loop().add_timer(promise);
        }

        void await_resume() const noexcept{}

        std::chrono::system_clock::time_point m_expire_time;
    };
    struct ReturnPreviousPromise{
        auto initial_suspend(){ return std::suspend_always{}; }
        auto final_suspend()noexcept { return PreviousAwaiter{m_previous};}
        void unhandled_exception(){
            throw;
        }
        void return_value(std::coroutine_handle<> previous) noexcept {
            m_previous = previous;
        }
        auto get_return_object(){
            return std::coroutine_handle<ReturnPreviousPromise>::from_promise(*this);
        }
        ReturnPreviousPromise& operator=(ReturnPreviousPromise&&) = delete;

        std::coroutine_handle<> m_previous;
    };
    struct ReturnPreviousTask{
        using promise_type = ReturnPreviousPromise;

        ReturnPreviousTask(std::coroutine_handle<promise_type> coro) noexcept 
        :m_coroutine(coro){};   

        ReturnPreviousTask(ReturnPreviousTask&&) = delete;
        ~ReturnPreviousTask(){ m_coroutine.destroy(); }
        
        std::coroutine_handle<> m_coroutine;
    };
    struct WhenAllCtrBlock{
        std::size_t m_count;
        std::coroutine_handle<> m_previous{};
        std::exception_ptr m_exception{};
    };

    struct WhenAllAwaiter{
        bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) const {
            if(all_handels.empty()) return h;
            m_control.m_previous = h;
            for(auto const &t: all_handels.subspan(1)){
                t.m_coroutine.resume();
            }            
            return all_handels.front().m_coroutine;
        }

        void await_resume() const {
            if(m_control.m_exception) [[unlikely]]{
                std::rethrow_exception(m_control.m_exception);
            }
        }

        WhenAllCtrBlock &m_control;
        std::span<ReturnPreviousTask const> all_handels;
    };
    

    inline Task<void, SleepUntilPromise> sleep_until(std::chrono::system_clock::time_point expireTime){
        co_await SleepAwaiter{expireTime};
    }

    inline Task<void, SleepUntilPromise> sleep_for(std::chrono::system_clock::duration duration){
        co_await sleep_until(std::chrono::system_clock::now() + duration);
    }



    template <class T>
    ReturnPreviousTask whenAllHelper(auto const &t, WhenAllCtrBlock &control,
                                    Uninitialized<T> &result) {
        try {
            if constexpr (std::is_void_v<T>) {
                co_await t;
                result.putValue(NonVoidHelper<>{});  // void 类型传递 NonVoidHelper<>
            } else {
                result.putValue(co_await t);
            }
        } catch (...) {
            control.m_exception = std::current_exception();
            co_return control.m_previous;
        }
        --control.m_count;
        if (control.m_count == 0) {
            co_return control.m_previous;
        }
        co_return nullptr;
    }

    template <std::size_t... Is, class... Ts>
    Task<std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>>
    whenAllImpl(std::index_sequence<Is...>, Ts &&...ts) {
        WhenAllCtrBlock control{sizeof...(Ts)};
        std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;
        ReturnPreviousTask taskArray[]{whenAllHelper(ts, control, std::get<Is>(result))...};
        co_await WhenAllAwaiter(control, taskArray);
        co_return std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>(
            std::get<Is>(result).moveValue()...);
    }

    template <Awaitable... Ts>
        requires(sizeof...(Ts) != 0)
    auto when_all(Ts &&...ts) {
        return whenAllImpl(std::make_index_sequence<sizeof...(Ts)>{},
                        std::forward<Ts>(ts)...);
    }

    struct WhenAnyCtlBlock {
        static constexpr std::size_t kNullIndex = std::size_t(-1);

        std::size_t mIndex{kNullIndex};
        std::coroutine_handle<> mPrevious{};
        std::exception_ptr mException{};
    };

    struct WhenAnyAwaiter {
        bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine) const {
            if (mTasks.empty()) return coroutine;
            mControl.mPrevious = coroutine;
            for (auto const &t: mTasks.subspan(1))
                t.m_coroutine.resume();
            return mTasks.front().m_coroutine;
        }

        void await_resume() const {
            if (mControl.mException) [[unlikely]] {
                std::rethrow_exception(mControl.mException);
            }
        }

        WhenAnyCtlBlock &mControl;
        std::span<ReturnPreviousTask const> mTasks;
    };

    template <class T>
    ReturnPreviousTask whenAnyHelper(auto const &t, WhenAnyCtlBlock &control,
                                    Uninitialized<T> &result, std::size_t index) {
        try {
            if constexpr (std::is_void_v<T>) {
                co_await t;
                result.putValue(NonVoidHelper<>{});  // void 类型传递 NonVoidHelper<>
            } else {
                result.putValue(co_await t);
            }
        } catch (...) {
            control.mException = std::current_exception();
            co_return control.mPrevious;
        }
        --control.mIndex = index;
        co_return control.mPrevious;
    }

    template <std::size_t... Is, class... Ts>
    Task<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>>
    whenAnyImpl(std::index_sequence<Is...>, Ts &&...ts) {
        WhenAnyCtlBlock control{};
        std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;
        ReturnPreviousTask taskArray[]{whenAnyHelper(ts, control, std::get<Is>(result), Is)...};
        co_await WhenAnyAwaiter(control, taskArray);
        Uninitialized<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>> varResult;
        ((control.mIndex == Is && (varResult.putValue(
            std::in_place_index<Is>, std::get<Is>(result).moveValue()), 0)), ...);
        co_return varResult.moveValue();
    }

    template <Awaitable... Ts>
        requires(sizeof...(Ts) != 0)
    auto when_any(Ts &&...ts) {
        return whenAnyImpl(std::make_index_sequence<sizeof...(Ts)>{},
                        std::forward<Ts>(ts)...);
    }

    template<class Loop, class T, class P = Promise<T>>
    T run_task(Loop &loop, Task<T, P> &task){
        auto a = task.operator co_await();
        a.await_suspend(std::noop_coroutine()).resume();
        while(loop.run());
        return a.await_resume();
    }
    template<class Loop, class P = Promise<void>>
    void run_task(Loop &loop, Task<void, P> &task){
        auto a = task.operator co_await();
        a.await_suspend(std::noop_coroutine()).resume();
        while(loop.run());
        a.await_resume();
    }
}
