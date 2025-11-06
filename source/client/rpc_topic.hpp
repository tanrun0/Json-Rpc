#pragma once
#include "requestor.hpp"

namespace TrRpc
{
    namespace client
    {
        class TopicManager // 提供 发送订阅消息的客户端和 订阅主题接受消息的客户端的所有功能
        {
        public:
            using ptr = std::shared_ptr<TopicManager>;
            // 收到 订阅的 key 主题 发布过来的 msg 消息的回调
            using SubCallback = std::function<void(const std::string &key, const std::string &msg)>;

            TopicManager(const Requestor::ptr &requestor)
                : _requestor(requestor) {}
            // 1. 构建对应的请求发送给服务端;  2. 维护好 client 的 TopicManager
            bool create(const BaseConnection::ptr &conn, const std::string &key)
            {
                // 创建主题，与 _topic_callbacks 无关
                return commonRequest(conn, key, TopicOptype::TOPIC_CREATE);
            }
            bool remove(const BaseConnection::ptr &conn, const std::string &key)
            {
                return commonRequest(conn, key, TopicOptype::TOPIC_REMOVE);
            }
            // 订阅主题，并且传入: 后续收到订阅主题发来的消息以后的回调函数
            bool subscribe(const BaseConnection::ptr &conn, const std::string &key, const SubCallback &cb)
            {
                addSubscribe(key, cb);
                bool ret = commonRequest(conn, key, TopicOptype::TOPIC_SUBSCRIBE);
                if (ret == false) // 如果订阅失败了，要删除掉对应的主题的消息回调
                    delSubscribe(key);
                return ret;
            }
            // 取消订阅
            bool cancel(const BaseConnection::ptr &conn, const std::string &key)
            {
                delSubscribe(key);
                return commonRequest(conn, key, TopicOptype::TOPIC_CANCEL);
            }
            // 向主题发送消息: 用于 消息发送客户端
            bool publish(const BaseConnection::ptr &conn, const std::string &key, const std::string &msg)
            {
                return commonRequest(conn, key, TopicOptype::TOPIC_PUBLISH, msg);
            }
            // 接收并处理 “来自服务端的发布消息” 的回调接口, 用于消息接收客户端
            void onPublish(const BaseConnection::ptr &conn, const TopicRequest::ptr &msg_req)
            {
                // 1. 确保收到的是: “发布的主题消息”
                auto optype = msg_req->optype();
                if (optype != TopicOptype::TOPIC_PUBLISH)
                {
                    ERR_LOG("收到了错误类型的主题操作");
                    return;
                }
                // 2. 取出主题名称，和消息内容
                std::string topic_key = msg_req->topickey();
                std::string topic_msg = msg_req->topicMsg();
                // 3. 通过主题名称，找到对应的回调函数进行处理
                auto callback = getSubscribe(topic_key);
                if (!callback)
                {
                    ERR_LOG("收到了 %s 主题消息，但是该消息无主题处理回调！", topic_key.c_str());
                    return;
                }
                return callback(topic_key, topic_msg);
            }
            // 针对不同操作生成不同的 Request 发送，并判断 请求是否处理成功
            bool commonRequest(const BaseConnection::ptr &conn, const std::string &key, const TopicOptype &optype, const std::string &msg = "")
            {
                // 1. 组织请求
                auto msg_req = MessageFactory::create<TopicRequest>();
                msg_req->setId(UUid::uuid());
                msg_req->setMtype(MType::REQ_TOPIC);
                msg_req->setOptype(optype);
                msg_req->setTopicKey(key);
                if (optype == TopicOptype::TOPIC_PUBLISH)
                {
                    msg_req->setTopicMsg(msg);
                }
                // 2. 发送请求, 等待响应
                // BaseMessage 抽象类不能被实例化，但是可以先声明指针，后续让它指向派生类实例
                BaseMessage::ptr msg_rsp;
                bool ret = _requestor->send(conn, msg_req, msg_rsp);
                if (ret == false)
                {
                    ERR_LOG("主题操作, 请求处理失败");
                    return false;
                }
                auto topic_rsp = std::dynamic_pointer_cast<TopicResponse>(msg_rsp);
                if (topic_rsp == nullptr)
                {
                    ERR_LOG("主题响应向下转换类型失败");
                    return false;
                }
                if (topic_rsp->rcode() != RCode::RCODE_OK)
                {
                    ERR_LOG("主题操作, 请求处理失败: %s", errReason(topic_rsp->rcode()).c_str());
                    return false;
                }
                return true;
            }
            // 提供便捷的操作 _topic_callbacks 的接口
            void addSubscribe(const std::string &key, const SubCallback &cb)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _topic_callbacks.emplace(key, cb);
            }
            void delSubscribe(const std::string &key)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _topic_callbacks.erase(key);
            }
            // 查找某个主题的回调函数
            const SubCallback getSubscribe(const std::string &key)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _topic_callbacks.find(key);
                if (it == _topic_callbacks.end())
                    return SubCallback();
                return it->second;
            }

        private:
            std::mutex _mutex;
            std::unordered_map<std::string, SubCallback> _topic_callbacks; // 管理: 收到不同主题消息后的 不同回调处理函数
            Requestor::ptr _requestor;                                     // 给客户端发请求要用这个对象的特殊send接口
        };
    }
}