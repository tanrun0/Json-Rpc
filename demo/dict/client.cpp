#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/net/EventLoopThread.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <iostream>
#include <string>
#include <unordered_map>

class DictClient
{
private:
    // EventLoop的 loop 是死循环(单线程的话走不动了)，client 要发数据给服务器的, 所以需要开一个线程给它运行
    muduo::net::EventLoopThread _loopthread;
    muduo::net::EventLoop *_baseloop;
    // 需要一个计数器, 因为 connect 操作是异步的，如果连接没建立好就发送数据会出错，所以要等一下
    muduo::CountDownLatch _downlatch;
    muduo::net::TcpClient _client;
    // 保存与服务器的连接对象 conn, 便于把数据发回去
    muduo::net::TcpConnectionPtr _conn;

private:
    // 连接建立/关闭的回调函数，内部自行判断是关闭了还是销毁了
    void OnConnection(const muduo::net::TcpConnectionPtr &conn) // muudo的可调用对象要求传递这个参数
    {
        // connected 返回连接状态
        if (conn->connected())
        {
            std::cout << "连接建立" << std::endl;
            _conn = conn;
            // 连接建立完成后才唤醒， 确保不会出现异步带来的错误
            _downlatch.countDown(); // 计数--，为0时唤醒阻塞
        }
        else
        {
            std::cout << "连接关闭" << std::endl;
            _conn.reset(); // 清空
        }
    }
    // 收到数据以后的业务处理回调函数 (也是可调用对象要求传这三个参数)
    void OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buf, muduo::Timestamp)
    {
        std::string msg = buf->retrieveAllAsString();
        std::cout << msg << std::endl; // 业务只是单纯打印
    }

public:
    // startLoop() 会开启 loop
    DictClient(std::string sip, int sport)
        : _baseloop(_loopthread.startLoop()), _downlatch(1), _client(_baseloop, muduo::net::InetAddress(sip, sport), "DictClient")
    {
        // 函数有几个参数我们现在不传，就要预留几个参数的位置
        _client.setConnectionCallback(std::bind(&DictClient::OnConnection, this, std::placeholders::_1));
        _client.setMessageCallback(std::bind(&DictClient::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        _client.connect();
        _downlatch.wait();
    }
    bool send(const std::string &msg)
    {
        if (_conn->connected() == false)
        {
            std::cout << "连接已经断开，发送数据失败！\n";
            return false;
        }
        _conn->send(msg); // 发数据给服务器
        return true;
    }
};

int main()
{
    DictClient client("127.0.0.1", 8085);
    while (1)
    {
        std::string msg;
        std::cin >> msg;
        client.send(msg);
    }
    return 0;
}