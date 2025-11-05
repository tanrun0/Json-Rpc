#pragma once
#include "net.hpp"
#include "message.hpp"
#include <unordered_map>

namespace TrRpc
{
    // 函数回调基类
    class Callback
    {
    public:
        using ptr = std::shared_ptr<Callback>;
        virtual void OnMessage(BaseConnection::ptr &conn, BaseMessage::ptr &msg) = 0;
    };
    // 借助基类实现多态
    // 这个 T 要改变的是: 回调函数的参数的类型; 这个 T 本身并不是那个参数类型，而是通过这个T定了那个参数类型
    template <typename T>
    class CallbackT : public Callback
    {
    public:
        using ptr = std::shared_ptr<CallbackT<T>>;
        using MessageCallback = std::function<void(BaseConnection::ptr &, std::shared_ptr<T> &)>;
        CallbackT(MessageCallback handler) : _handler(handler) {}
        // 外界设置的时候还是像原来那样，不过我们自己在内部进行改变
        // 注册到 map 里的时候也是我们 “转换后的”, 取出来的时候也调用特定的
        void OnMessage(BaseConnection::ptr &conn, BaseMessage::ptr &msg) override
        {
            auto t_msg = std::dynamic_pointer_cast<T>(msg);
            return _handler(conn, t_msg);
        }
    private:
        MessageCallback _handler;
    };
    class Dispatcher
    {
    public:
        using ptr = std::shared_ptr<Dispatcher>;
        template<typename T> // 支持接受不同类型的可调用对象(区别是可调用对象的参数msg类型不同)
        // 对不同消息的分发处理注册
        void registerHandler(MType mtype, const typename CallbackT<T>::MessageCallback &handler)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            Callback::ptr cb = std::make_shared<CallbackT<T>>(handler);
            _handlers.insert(std::make_pair(mtype, cb));
        }

        // 提供给 client/server 设置的 收到任何消息的入口，  (内部根据具体的消息类型调用到上面 registerHandler 注册好的不同的回调函数)
        void OnMessage(BaseConnection::ptr &conn, BaseMessage::ptr &msg)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            MType mtype = msg->mtype();
            auto it = _handlers.find(mtype);
            // 没找到，理论上是不存在的，因为服务端和客户端都是我们写的 (除非遇到恶意客户端访问未知方法)
            if(it == _handlers.end()) 
            {
                ERR_LOG("收到未知消息类型: %d", (int)mtype);
                conn->shutdown();
                return;
            }
            // 通过父类指针调用到不同子类的 OnMessage 方法
            it->second->OnMessage(conn, msg);
        }
    private:
        std::mutex _mutex; // 用来给操作 _handlers 的时候加锁
        // 这种方法: MessageCallback 里面 BaseMessage::ptr& 父类无法访问到子类的成员
        // 解决 1: 设置OneMessage时，在每一个里面 做 dynamic_pointer_cast 强转(这样会增加使用者负担)
        // 解决 2: 让 map 直接映射到不同的可调用对象?
        //          2.1 直接模版不行，因为: map 不支持第二参数是不同类型的
        //          2.2 有什么方法可以让 map 存相同，但是表现不同呢？ --> 多态
        //          2.3 设计一个父类 Callback，但是派生出子类 CallbackT (用父类指针指向子类对象)
        //          2.4 我们就可以让 CallbackT 成为模板类，根据具体类型，在内部的 OnMessage 函数里自动转换 BaseMessage::ptr 成为具体的
        //          2.5 设置时，直接支持我们设置 不同类型 Message 的 OnMessage
        //          2.6 调用时，虽然都是父类指针，但可利用多态表现出来
        // std::unordered_map <MType, MessageCallback> _handlers;
        std::unordered_map<MType, Callback::ptr> _handlers;
    };
    class DispatcherFactory
    {
    public:
        template <typename... Args>
        static Dispatcher::ptr create(Args &&...args)
        {
            return std::make_shared<Dispatcher>(std::forward<Args>(args)...);
        }
    };
}
