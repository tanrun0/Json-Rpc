#pragma once
#include <memory>
#include <iostream>
#include <string>
#include <functional>
#include "fields.hpp"
// 实现抽象层：设置好各模块的基类，具体的实现由子类继承实现

// 通信抽象实现
// 原始数据格式: |--len--|--mtype--|--idlen--|--id--|--body--|
// BaseMessage: 被 protocol 解析后，直接存有: id, mytpe, body 成员
// BaseMessage：是业务层消息抽象，里面存储着业务的核心消息, 如:ID, MType, 以及核心业务数据 body字段(都是被 protocol 反序列化后的), 是上层业务代码的处理对象
// BaseBuffer：是传输层缓冲区抽象，存储的是底层通信中 “原始字节数据”
// BaseProtocol：协议抽象，如：BaseProtocol 通过 BaseBuffer 读取原始字节，解析出 BaseMessage  （主要负责：缓冲数据和业务消息的转换）

// BaseConnection：对连接进行抽象
// BaseServer：对服务端进行抽象
// BaseClient：对客户端进行抽象

namespace TrRpc
{
    class BaseMessage
    {
    public:
        using ptr = std::shared_ptr<BaseMessage>;
        // 如果后续要用父类指针指向子类对象，然后销毁，需要把父类析构设置成虚函数，不然可能导致没有调用子类的析构，子类成员销毁不了
        virtual ~BaseMessage() {}
        virtual void setId(const std::string &rid) { _rid = rid; } // 这种都相同的接口可以提前实现
        virtual void setMtype(MType mtype) { _mtype = mtype; }
        virtual std::string rid() { return _rid; }
        virtual MType mtype() { return _mtype; }

        virtual std::string serialize() = 0;
        virtual bool deserialize(const std::string &msg) = 0;
        virtual bool check() = 0; // 检查消息的合法性，验证当前消息对象的内容是否符合其业务场景的约定格式或规则
    private:
        std::string _rid;
        MType _mtype;
    };

    class BaseBuffer
    {
    public:
        using ptr = std::shared_ptr<BaseBuffer>;
        virtual ~BaseBuffer() {}
        virtual size_t readablesize() = 0;                    // 可读空间大小
        virtual int32_t peekInt32() = 0;                      // 读取 4 个字节，但是不移动指针
        virtual void retrieveInt32() = 0;                     // 跳过 4 个字节，不返回数据 (和 peekInt32 搭配使用)
        virtual int32_t readInt32() = 0;                      // 读取 4 个字节，返回，并且移动指针
        virtual std::string retrieveAsString(size_t len) = 0; // 把所有数据当做字符串读出来

        // 不需要真正的存储成员变量buffer，由外界自己决定存储方式
    };

    class BaseProtocol
    {
    public:
        using ptr = std::shared_ptr<BaseProtocol>;
        virtual ~BaseProtocol() {}
        virtual bool canProcessed(const BaseBuffer::ptr &buf) = 0;                     // 缓冲数据是否符合协议格式(符合了才能被本协议处理)
        virtual bool onMessage(const BaseBuffer::ptr &buf, BaseMessage::ptr &msg) = 0; // 从缓冲数据中提取业务消息
        virtual std::string serialize(const BaseMessage::ptr &msg) = 0;                // 把业务消息转换为符合协议格式的字节流（做发送到缓冲区的准备）
    };

    class BaseConnection
    {
    public:
        using ptr = std::shared_ptr<BaseConnection>;
        virtual ~BaseConnection() {}
        virtual void send(const BaseMessage::ptr &msg) = 0;
        virtual void shutdown() = 0;
        virtual bool connected() = 0;
    };

    using ConnectionCallback = std::function<void(BaseConnection::ptr &)>;
    using CloseCallback = std::function<void(BaseConnection::ptr &)>;
    using MessageCallback = std::function<void(BaseConnection::ptr &, BaseMessage::ptr &)>;
    class BaseServer
    {
    public:
        using ptr = std::shared_ptr<BaseServer>;
        virtual void SetConnectionCallback(const ConnectionCallback &cb)
        {
            _cb_connection = cb;
        }
        virtual void SetCloseCallback(const CloseCallback &cb)
        {
            _cb_close = cb;
        }
        virtual void SetMessageCallback(const MessageCallback &cb)
        {
            _cb_message = cb;
        }
        virtual void start() = 0;

    protected: // 子类能访问，类外不能访问
        ConnectionCallback _cb_connection;
        CloseCallback _cb_close;
        MessageCallback _cb_message;
    };

    class BaseClient
    {
    public:
        using ptr = std::shared_ptr<BaseClient>;
        // 也需要回调
        virtual void SetConnectionCallback(const ConnectionCallback &cb)
        {
            _cb_connection = cb;
        }
        virtual void SetCloseCallback(const CloseCallback &cb)
        {
            _cb_close = cb;
        }
        virtual void SetMessageCallback(const MessageCallback &cb)
        {
            _cb_message = cb;
        }
        // 也有连接
        virtual void connect() = 0;                         // 建立连接
        virtual void shutdown() = 0;                        // 关闭连接
        virtual bool send(const BaseMessage::ptr &msg) = 0; // 发送数据
        virtual BaseConnection::ptr connection() = 0;       // 获取与服务器的连接对象 conn, 便于把数据发回去
        virtual bool connected() = 0;                       // 判断连接是否"正常"

    protected: // 子类能访问，类外不能访问
        ConnectionCallback _cb_connection;
        CloseCallback _cb_close;
        MessageCallback _cb_message;
    };
}