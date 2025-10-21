#include <iostream>
#include <thread>
#include <future>


int Add(int a, int b)
{
    std::cout << "Into Add" << std::endl;
    return a + b;
}
int main()
{
    // 实例化要传入"结果"类型, future 对象会保存结果
    // async: 直接异步执行，把结果存储在 future中;
    // deferred: 在 get处才同步调用 Add 
    // std::future<int> res = std::async(std::launch::async, Add, 2, 3);
    std::future<int> res = std::async(std::launch::deferred, Add, 2, 3);
    std::this_thread::sleep_for(std::chrono::seconds(1)); // 主线程睡眠一秒
    std::cout << "------------------------" << std::endl;
    // 如果是 async, 在这里获取结果，没有结果会阻塞
    // 如果是 deferred, 在这里才开始调用 Add 同步执行
    int ans = res.get();
    std::cout << ans << std::endl;
    return 0;
}