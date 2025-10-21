#include <iostream>
#include <thread>
#include <future>
#include <memory>

int Add(int a, int b)
{
    std::cout << "Into Add" << std::endl;
    return a + b;
}

// 支持将结果设置进 promise, 可以通过关联 future 在别的流里面获取
// pro也不支持拷贝
int main()
{
    // 创建 pro
    std::promise<int> pro;
    // future 对象
    auto fut = pro.get_future();
    // 创建一个线程执行 Add, 并且设置结果
    std::thread th([&pro](){
        int ans = Add(11, 22);
        pro.set_value(ans);
    });
    // 会阻塞到: 直到 关联的 promise 被设置
    std::cout << fut.get() << std::endl;
    th.join();
    return 0;
}