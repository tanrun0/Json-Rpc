#include <muduo/net/TcpServer.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <iostream>
#include <string>
#include <unordered_map>


std::unordered_map<std::string, std::string> dict = {
    {"hello", "你好"}, {"apple", "苹果"}, {"banana", "香蕉"}
};


class DictServer
{
private:
    // 一个 Reactor  == 一个线程 + 一个 EventLoop，EventLoop 构造时绑定线程
    // 

    // EventLoop：事件循环模块：循环等待 → 监控事件(是否就绪) → 事件触发 → 执行回调
    // 对于主 Reactor 而言，主要监控的是: 监听套接字是否读就绪，读就绪就是有新连接到来了
    muduo::net::EventLoop _baseloop; // 会先初始化这个，调用默认构造
    muduo::net::TcpServer _server;

private:
    // 连接建立/关闭的回调函数，内部自行判断是关闭了还是销毁了
    void OnConnection(const muduo::net::TcpConnectionPtr& conn) // muudo的可调用对象要求传递这个参数
    {
        // connected 返回连接状态
        if(conn->connected())
            std::cout << "连接建立" << std::endl;
        else
            std::cout << "连接关闭" << std::endl;
    }
    // 收到数据以后的业务处理回调函数 (也是可调用对象要求传这三个参数)
    void OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer *buf, muduo::Timestamp)
    {
        std::string msg = buf->retrieveAllAsString();
        std::string res;
        auto it = dict.find(msg);
        if(it == dict.end())
            res = "未知单词";
        else
            res = dict[msg];
        return conn->send(res);
    }

public:
    // TcpServer::start() 方法的调用链里，分为 “创建监听套接字” 和 “注册事件到 EventLoop” 两步
    // 我们在 TcpServer 的构造函数里面要传入一个 eventloop 就是为了把监听套接字传给 eventloop 进行监控是吗
    DictServer(int port)
        : _server(&_baseloop, muduo::net::InetAddress("0.0.0.0", port),
                  "DictServer", muduo::net::TcpServer::kNoReusePort)
    {
        // 函数有几个参数我们现在不传，就要预留几个参数的位置
        _server.setConnectionCallback(std::bind(&DictServer::OnConnection, this, std::placeholders::_1));
        _server.setMessageCallback(std::bind(&DictServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }
    void Start()
    {
        _server.start(); // 先开始监听
        _baseloop.loop(); // 才能开始循环，进行监控和事件触发(回调)
    }
};

int main()
{
    DictServer dictserver(8085);
    dictserver.Start();
    return 0;
}