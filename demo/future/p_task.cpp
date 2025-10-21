#include <iostream>
#include <thread>
#include <future>
#include <memory>

int Add(int a, int b)
{
    std::cout << "Into Add" << std::endl;
    return a + b;
}

// packaged_task 用于封装一个可异步执行(放入别的线程执行)的任务
// 注意: 1. 结果需要通过关联的 future 来获取; 先 get_future再执行，不然 p_task销毁了就无了
//       2. 不建议作为线程入口函数(即：直接绑定线程)， 因为:
//           2.1  一个任务一个线程，频繁创建线程 (所以建议丢进线程池里面)
//           2.2 有 “任务未执行就销毁” 的风险, 
//               因为: 被包装的对象在另一个线程执行，但是如果主线程中 p_task: 因为程序结束而导致生命周期结束      
//               可以用： 1. 移动来改变任务的所属权，2. 可以用shared_ptr来增加引用计数
//       3. p_task 不支持拷贝但支持移动, 也可以用智能指针管理来实现拷贝

int main()
{
    // 1. 包装任务
    auto task = std::make_shared<std::packaged_task<int(int, int)>>(Add); // Add 传入构造 p_task
    // 2. 获取 future
    auto fut = (*task).get_future();
    // 3. 放到线程里异步执行 (这里暂时: 一任务一线程, 但用 lambda 来保证安全)
    // 值传递，shared_ptr 引用计数 +1
    std::thread t([task](){
        (*task)(11, 22);
    });
    // 4. 获取结果(当然，这里也会对 task 的销毁安全问题做保障), 会阻塞到 task 执行完
    std::cout << fut.get() << std::endl;
    // 5. C++层面, 要std::thread 对象的状态清理，必须join(不管子线程有没有结束), 里面也封装了系统调用 pthread_join()
    t.join();
    return 0;
}