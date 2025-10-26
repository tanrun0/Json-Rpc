#pragma once
#include "abstract.hpp"
#include "fields.hpp"
#include "detail.hpp"
#include "message.hpp"
#include <muduo/net/Buffer.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThread.h>
#include <muduo/net/EventLoop.h>
#include <unordered_map>
#include <mutex>
#include <arpa/inet.h>

namespace TrRpc
{
    // 不对资源进行管理（即：不能释放buffer）
    // 只是提供操作底层缓冲数据的接口（底层缓冲区也是muduo的缓冲区，对muduo接口进行封装）
    class MuduoBuffer : public BaseBuffer
    {
    public:
        MuduoBuffer(muduo::net::Buffer *buf) : _buf(buf)
        {
        }
        virtual size_t readablesize()
        {
            return _buf->readableBytes();
        }
        virtual int32_t peekInt32()
        {
            // muduo库是一个网络库，从缓冲区取出一个4字节整形，会进行网络字节序的转换
            return _buf->peekInt32();
        }
        virtual void retrieveInt32()
        {
            return _buf->retrieveInt32();
        }
        virtual int32_t readInt32()
        {
            return _buf->readInt32();
        }
        virtual std::string retrieveAsString(size_t len)
        {
            return _buf->retrieveAsString(len);
        }

    private:
        muduo::net::Buffer *_buf;
    };
    class BufferFactory
    {
    public:
        template <typename... Args>
        static BaseBuffer::ptr create(Args &&...args)
        {
            return std::make_shared<MuduoBuffer>(std::forward<Args>(args)...);
        }
    };
    // LV: Length-Value  前缀长度 + 格式化字段
    class LVProtocol : public BaseProtocol
    {
    public:
        using ptr = std::shared_ptr<LVProtocol>;
        // 判断数据大小(由请求头部字段定)是否够一条请求
        virtual bool canProcessed(const BaseBuffer::ptr &buf)
        {
            if(buf->readablesize() < lenFieldlength) // 连描述长的的四字节都不够
                return false;
            int total_len = buf->peekInt32();
            if (buf->readablesize() < total_len + lenFieldlength)
                return false;
            return true;
        }
        // 解析 buf 得到一个 msg(mtype, id, 反序列化后的body)
        virtual bool onMessage(const BaseBuffer::ptr &buf, BaseMessage::ptr &msg)
        {
            int32_t total_len = buf->readInt32();  // 读取并移除正文长度信息
            MType mtype = (MType)buf->readInt32(); // 再读四个是 Mtype
            int32_t idlen = buf->readInt32();      // 读取id长度(id 格式自己设置的，所以长度可能不一)
            std::string id = buf->retrieveAsString(idlen);
            int body_len = total_len - mtypeFieldlength - idlenFieldlength - idlen;
            std::string body = buf->retrieveAsString(body_len);
            msg = MessageFactory::create(mtype); // 构建业务消息对象
            if (msg.get() == nullptr)            // 获取原生指针才能比较
            {
                ERR_LOG("消息类型错误, 构造消息对象失败");
                return false;
            }
            msg->setId(id);
            msg->setMtype(mtype);
            bool ret = msg->deserialize(body); // 反序列化好后，业务的核心数据就已经在 msg 这个消息对象里面了
            if (ret == false)
            {
                ERR_LOG("反序列化失败");
                return false;
            }
            return true;
        }
        // 把所有数据组织成像原生数据一样[简单来说就是上面解析的逆操作]
        // 注意：序列化的时候要序列化回网络序列(因为要放入 muduo 网络库的 buf 中)
        // 接口:        uint32_t htonl(uint32_t hostlong);
        // 主要是：原来使用 muduo 的 ReadInt32 出来的字段
        virtual std::string serialize(const BaseMessage::ptr &msg)
        {
            std::string body = msg->serialize();
            std::string id = msg->rid();
            int32_t idlen = id.size();
            // 注意这里不要计算成网络字节序的长度了
            int32_t h_total_len = mtypeFieldlength + idlenFieldlength + idlen + body.size();
            std::string str;
            str.reserve(h_total_len + lenFieldlength);
            // 添加的时候要转回网络字节序
            int32_t n_total_len = htonl(h_total_len);
            str.append((char *)&n_total_len, lenFieldlength); // 从给的地址开始，往后加len长（把数字强转，然后像字符一样添加进去）
            int32_t mtype = htonl((uint32_t)msg->mtype());
            str.append((char *)&mtype, mtypeFieldlength);
            idlen = htonl(idlen);
            str.append((char *)&idlen, idlenFieldlength);
            str.append(id);
            str.append(body);
            return str;
        }

    private:
        const size_t lenFieldlength = 4;
        const size_t mtypeFieldlength = 4;
        const size_t idlenFieldlength = 4;
    };
    class LVProtocolFactory
    {
    public:
        template <typename... Args>
        static BaseProtocol::ptr create(Args &&...args)
        {
            // forward 每次只能处理一个参数的转发
            // ...表示: 展开且对每个参数都用 forward
            return std::make_shared<LVProtocol>(std::forward<Args>(args)...);
        }
    };
    class MuduoConnection : public BaseConnection
    {
    public:
        using ptr = std::shared_ptr<MuduoConnection>;
        // 构造函数参数顺序改为 (conn, protocol)，与 ConnectionFactory 调用处保持一致
        MuduoConnection(const muduo::net::TcpConnectionPtr &conn, const BaseProtocol::ptr &protocol)
            : _protocol(protocol), _conn(conn)
        {
        }
        virtual void send(const BaseMessage::ptr &msg) override
        {
            // 通过 protocol 数据序列化(成符合LV协议格式的字节流)以后发送
            std::string data = _protocol->serialize(msg);
            _conn->send(data);
        }
        virtual void shutdown() override
        {
            _conn->shutdown();
        }
        virtual bool connected() override
        {
            return _conn->connected();
        }

    private:
        BaseProtocol::ptr _protocol;        // 但是没有必要每个 connection 都配置一个不同的protocol
        muduo::net::TcpConnectionPtr _conn; // 基于muduo库的conn实现
    };
    class ConnectionFactory
    {
    public:
        template <typename... Args>
        static BaseConnection::ptr create(Args &&...args)
        {
            return std::make_shared<MuduoConnection>(std::forward<Args>(args)...);
        }
    };

    class MuduoServer : public BaseServer
    {
    public:
        using ptr = std::shared_ptr<MuduoServer>;
        MuduoServer(int port)
            : _protocol(LVProtocolFactory::create()), _server(&_baseloop, muduo::net::InetAddress("0.0.0.0", port),
                                                              "MuduoServer", muduo::net::TcpServer::kNoReusePort) {}

        // 设置回调的接口继承了父类，是有的
        virtual void start()
        {
            _server.setConnectionCallback(std::bind(&MuduoServer::OnConnection, this, std::placeholders::_1));
            _server.setMessageCallback(std::bind(&MuduoServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            _server.start();
            _baseloop.loop();
        }

    private:
        // 连接建立/关闭的回调函数，内部自行判断是关闭了还是销毁了
        // 我们在这里相当于对回调函数进行了进一步封装，统一基础行为
        // 从而又保留设置回调的入口，用户可以自行再扩展
        // 用户的回调的操作对象是: BaseConnection, 不用 muduo 库时，回调函数设置的接口不用改
        void OnConnection(const muduo::net::TcpConnectionPtr &conn) // muudo的可调用对象要求传递这个参数
        {
            // connected 返回连接状态
                if (conn->connected())
                {
                    std::cout << "连接建立" << std::endl;
                    auto base_conn = ConnectionFactory::create(conn, _protocol); // 生成 base_conn，传入协议
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    _conns.insert(std::make_pair(conn, base_conn));
                }
                // 连接建立成功时的回调函数，如果有就调用
                if (_cb_connection)
                    _cb_connection(base_conn);
            }
            else
            {
                auto base_conn = ConnectionFactory::create(conn, _protocol);
                std::cout << "连接关闭" << std::endl;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _conns.find(conn);
                    if (it == _conns.end())
                        return;
                    base_conn = it->second;
                    _conns.erase(it);
                    if (_cb_close)
                        _cb_close(base_conn);
                }
            }
        }
        // 收到数据以后的业务处理回调函数 (也是可调用对象要求传这三个参数)
        void OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buf, muduo::Timestamp)
        {
            DBG_LOG("连接有数据到来, 立即处理");
            auto base_buf = BufferFactory::create(buf);
            while (1) // 有可能一次有多条完整的请求数据
            {
                if (_protocol->canProcessed(base_buf) == false)
                {
                    // 不满足一条请求的要求，但是数据很多
                    if (base_buf->readablesize() > maxDataSize)
                    {
                        conn->shutdown(); // 这里采取极端做法，直接关
                        ERR_LOG("缓冲区中数据过大! ");
                        return;
                    }
                    // 数据量不足, 正常返回等数据够
                    return;
                }
                // 代表有一条请求，但是内部格式不能确保正确
                BaseMessage::ptr base_msg;
                bool ret = _protocol->onMessage(base_buf, base_msg);
                if (ret == false)
                {
                    ERR_LOG("请求数据错误, 不符合协议");
                    conn->shutdown();
                    return;
                }
                // 代表反序列化成功, 核心业务数据已经在 base_msg里了
                BaseConnection::ptr base_conn;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _conns.find(conn);
                    if (it == _conns.end())
                    {
                        conn->shutdown();
                        return;
                    }
                    base_conn = _conns[conn];
                }
                if (_cb_message) // 调用业务处理回调函数
                    _cb_message(base_conn, base_msg);
            }
        }

    private:
        const size_t maxDataSize = (1 << 16); // 用于判断请求数据是太长而错误
        BaseProtocol::ptr _protocol;          // 协议工具, 我们让conn共享这一个实例，避免资源浪费
        muduo::net::EventLoop _baseloop;
        muduo::net::TcpServer _server;
        // 还需要一个到 BaseConnection的映射, 因为回调函数接受的参数是 BaseConnection的
        // 避免回调函数和 muduo 库的强绑定
        // 这个是个临界资源，操作的时候注意加锁
        std::unordered_map<muduo::net::TcpConnectionPtr, BaseConnection::ptr> _conns;
        std::mutex _mutex;
    };

    class ServerFactory
    {
    public:
        template <typename... Args>
        static BaseServer::ptr create(Args &&...args)
        {
            return std::make_shared<MuduoServer>(std::forward<Args>(args)...);
        }
    };

    class MuduoClient : public BaseClient
    {
    public:
        using ptr = std::shared_ptr<MuduoClient>;
        MuduoClient(std::string sip, int sport)
            : _protocol(LVProtocolFactory::create()), _baseloop(_loopthread.startLoop()),
              _downlatch(1), _client(_baseloop, muduo::net::InetAddress(sip, sport), "MuduoClient")
        {
        }
        void connect()
        {
            _client.setConnectionCallback(std::bind(&MuduoClient::OnConnection, this, std::placeholders::_1));
            _client.setMessageCallback(std::bind(&MuduoClient::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            _client.connect();
            _downlatch.wait();
            DBG_LOG("连接服务器成功");
        }
        virtual bool send(const BaseMessage::ptr &msg) override
        {
            if (_conn->connected() == false)
            {
                std::cout << "连接已经断开，发送数据失败！\n";
                return false;
            }
            _conn->send(msg); // 发数据给服务器
            return true;
        }
        virtual void shutdown() override
        {
            _client.disconnect();
        }
        virtual BaseConnection::ptr connection() override
        {
            return _conn;
        }
        virtual bool connected()
        {
            return (_conn && _conn->connected());
        }

    private:
        // 连接建立/关闭的回调函数，内部自行判断是关闭了还是销毁了
        void OnConnection(const muduo::net::TcpConnectionPtr &conn) // muudo的可调用对象要求传递这个参数
        {
            // connected 返回连接状态
            if (conn->connected())
            {
                std::cout << "连接建立" << std::endl;
                _conn = ConnectionFactory::create(conn, _protocol);
                if (_cb_connection)
                    _cb_connection(_conn);
                _downlatch.countDown(); // 计数--，为0时唤醒阻塞
            }
            else
            {
                std::cout << "连接关闭" << std::endl;
                if (_cb_close && _conn)
                    _cb_close(_conn);
                _conn.reset(); // 清空
            }
        }
        // 收到数据以后的业务处理回调函数(其实和服务端一样，收到的都是网络字节序，在 buf 里)
        void OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buf, muduo::Timestamp)
        {
            DBG_LOG("连接有数据到来, 客户端立即处理");
            auto base_buf = BufferFactory::create(buf);
            while (1) // 有可能一次有多条完整的请求数据
            {
                if (_protocol->canProcessed(base_buf) == false)
                {
                    // 不满足一条请求的要求，但是数据很多
                    if (base_buf->readablesize() > maxDataSize)
                    {
                        conn->shutdown(); // 这里采取极端做法，直接关
                        ERR_LOG("缓冲区中数据过大! ");
                        return;
                    }
                    // 数据量不足, 正常返回等数据够
                    return;
                }
                // 代表有一条请求，但是内部格式不能确保正确
                BaseMessage::ptr base_msg;
                bool ret = _protocol->onMessage(base_buf, base_msg);
                if (ret == false)
                {
                    ERR_LOG("请求数据错误, 不符合协议");
                    conn->shutdown();
                    return;
                }
                if (_cb_message) // 调用业务处理回调函数
                    _cb_message(_conn, base_msg);
            }
        }

    private:
        const size_t maxDataSize = (1 << 16); // 用于判断请求数据是太长而错误
        BaseProtocol::ptr _protocol;
        muduo::net::EventLoopThread _loopthread;
        muduo::net::EventLoop *_baseloop;
        muduo::CountDownLatch _downlatch;
        muduo::net::TcpClient _client;
        BaseConnection::ptr _conn; // 保存与服务端的连接
    };
    class ClientFactory
    {
    public:
        template <typename... Args>
        static BaseClient::ptr create(Args &&...args)
        {
            return std::make_shared<MuduoClient>(std::forward<Args>(args)...);
        }
    };
}
