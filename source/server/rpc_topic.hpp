#pragma once
#include "../common/net.hpp"
#include <unordered_set>

namespace TrRpc
{
    namespace server
    {
        class TopicManager
        {
        public:
            using ptr = std::shared_ptr<TopicManager>;
            // 请求处理回调，根据 msg 的操作类型，决定执行什么操作(订阅 / 取消订阅 / 推送 ....)
            void onTopicRequest(const BaseConnection::ptr &conn, const TopicRequest::ptr &msg)
            {
                TopicOptype topic_optype = msg->optype();
                bool ret = true;
                switch (topic_optype)
                {
                // 主题的创建
                case TopicOptype::TOPIC_CREATE:
                    topicCreate(conn, msg);
                    break;
                // 主题的删除
                case TopicOptype::TOPIC_REMOVE:
                    topicRemove(conn, msg);
                    break;
                // 主题的订阅
                case TopicOptype::TOPIC_SUBSCRIBE:
                    ret = topicSubscribe(conn, msg);
                    break;
                // 主题的取消订阅
                case TopicOptype::TOPIC_CANCEL:
                    topicCancel(conn, msg);
                    break;
                // 主题消息的发布
                case TopicOptype::TOPIC_PUBLISH:
                    ret = topicPublish(conn, msg);
                    break;
                default:
                    return errorResponse(conn, msg, RCode::RCODE_INVALID_OPTYPE);
                }
                if (!ret)
                    return errorResponse(conn, msg, RCode::RCODE_NOT_FOUND_TOPIC);
                return topicResponse(conn, msg);
            }
            // 一个订阅者在连接断开时的处理---删除其关联的数据(如：删除对应主题中的订阅者，避免推送时推送给已取消订阅的...)
            void onShutdown(const BaseConnection::ptr &conn)
            {
                // 如果断开连接的不是订阅者: 直接返回
                std::vector<Topic::ptr> topics;
                Subscriber::ptr subscriber;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto sub_it = _subscribers.find(conn);
                    if (sub_it == _subscribers.end()) // 不是订阅者
                        return;
                    else
                        subscriber = sub_it->second;
                    // 移除订阅者映射信息
                    _subscribers.erase(sub_it);
                    for (auto topic : subscriber->topics)
                    {
                        auto topic_it = _topics.find(topic); // 进一步确定主题是否有注册
                        if (topic_it == _topics.end())
                            continue;
                        // 如果是这个订阅者订阅的主题，且主题有注册，则在主题里删除这个订阅者(这里先暂存，避免长时间占用锁)
                        topics.emplace_back(topic_it->second);
                    }
                }
                // 从主题中移除订阅者信息
                for (auto &topic : topics)
                {
                    topic->removeSubscriber(subscriber);
                }
            }

        private:
            void errorResponse(const BaseConnection::ptr& conn, const BaseMessage::ptr& msg, RCode rcode)
            {
                auto msg_rsp = MessageFactory::create<TopicResponse>();
                msg_rsp->setId(msg->rid());
                msg_rsp->setMtype(MType::RSP_TOPIC);
                msg_rsp->setRcode(rcode); // 不用关心是什么操作，只需要关心成功还是失败
                conn->send(msg_rsp);
            }
            void topicResponse(const BaseConnection::ptr& conn, const BaseMessage::ptr& msg)
            {
                auto msg_rsp = MessageFactory::create<TopicResponse>();
                msg_rsp->setId(msg->rid());
                msg_rsp->setMtype(MType::RSP_TOPIC);
                msg_rsp->setRcode(RCode::RCODE_OK);
                conn->send(msg_rsp);
            }
            // 根据主题创建请求(msg)来创建主题, conn用不到，但是为了同一接口，就留着
            void topicCreate(const BaseConnection::ptr &conn, const TopicRequest::ptr &msg)
            {
                // 构建一个 topic 对象然后添加进去
                std::string topic_name = msg->topickey();
                Topic::ptr topic = std::make_shared<Topic>(topic_name);
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    _topics.emplace(topic_name, topic); // 直接在 map 里构造元素(更快), 如果已经存在键值对，则什么都不做(不会覆盖)
                    // _topics.insert(std::make_pair(topic_name, topic)); // 先构造临时 pair, 再拷贝到 map 中
                }
            }
            void topicRemove(const BaseConnection::ptr &conn, const TopicRequest::ptr &msg)
            {
                // 既要删除主题的信息，也要移除相关订阅者中对该主题的订阅
                std::string topic_name = msg->topickey();
                std::vector<Subscriber::ptr> subscribers; // 临时存储有订阅了该主题的订阅者（后续删除订阅者关心的主题时，可以释放TopicManager的锁）
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _topics.find(topic_name);
                    if (it == _topics.end())
                        return;
                    Topic::ptr topic = it->second;
                    // 拷贝订阅者列表（避免后续操作持有 _mutex 锁）
                    subscribers.assign(topic->subscribers.begin(), topic->subscribers.end());
                    // 2. 删除这个主题的信息
                    _topics.erase(topic_name);
                }
                for (auto &sub : subscribers)
                {
                    sub->removeTopic(topic_name);
                }
            }
            // 主题订阅：订阅者关注主题 + 1  and 主题下订阅者 + 1
            bool topicSubscribe(const BaseConnection::ptr &conn, const TopicRequest::ptr &msg)
            {
                Topic::ptr topic;
                Subscriber::ptr sub;
                std::string topic_name = msg->topickey();
                {
                    // 1. 先判断订阅的主题是否存在，如果不存在则报错
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto topic_it = _topics.find(topic_name);
                    if (topic_it == _topics.end())
                    {
                        ERR_LOG("订阅了不存在的主题! ");
                        return false;
                    }
                    else
                        topic = topic_it->second;
                    // 2. 订阅者关注的主题 + 1.  如果订阅者信息本身不存在: 创建订阅者
                    auto sub_it = _subscribers.find(conn);
                    if (sub_it == _subscribers.end()) // 订阅者不存在，创建新订阅者
                    {
                        sub = std::make_shared<Subscriber>(conn);
                        _subscribers.emplace(conn, sub);
                    }
                    else // 订阅者存在
                        sub = sub_it->second;
                }
                // 订阅者关注主题 + 1
                sub->appendTopic(topic_name);
                // 3. 添加对应订阅者到对应主题
                topic->appendSubscriber(sub);
                return true;
            }
            // 主题取消订阅: 订阅者主题 -1  and  主题下订阅者 -1
            void topicCancel(const BaseConnection::ptr &conn, const TopicRequest::ptr &msg)
            {
                Topic::ptr topic;
                Subscriber::ptr sub;
                std::string topic_name = msg->topickey();
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto topic_it = _topics.find(topic_name);
                    if (topic_it == _topics.end())
                        return;
                    else
                        topic = topic_it->second;
                    auto sub_it = _subscribers.find(conn);
                    if (sub_it == _subscribers.end())
                        return;
                    else
                        sub = sub_it->second;
                }
                sub->removeTopic(topic_name);
                topic->removeSubscriber(sub);
            }
            bool topicPublish(const BaseConnection::ptr &conn, const TopicRequest::ptr &msg)
            {
                Topic::ptr topic;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto topic_it = _topics.find(msg->topickey());
                    if (topic_it == _topics.end())
                    {
                        return false;
                    }
                    topic = topic_it->second;
                }
                topic->pushMessage(msg);
                return true;
            }
            struct Subscriber
            {
                using ptr = std::shared_ptr<Subscriber>;
                std::mutex _mutex;
                BaseConnection::ptr conn;
                std::unordered_set<std::string> topics; // 订阅的主题
                Subscriber(BaseConnection::ptr c)
                    : conn(c) {}
                void appendTopic(const std::string &topic_name)
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    topics.insert(topic_name);
                }
                void removeTopic(const std::string &topic_name)
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    topics.erase(topic_name);
                }
            };

            struct Topic
            {
                using ptr = std::shared_ptr<Topic>;
                std::mutex _mutex;
                std::string topic_name;
                std::unordered_set<Subscriber::ptr> subscribers; // 当前主题的订阅者
                Topic(const std::string &name) : topic_name(name) {}
                // 新增订阅的时候调用
                void appendSubscriber(const Subscriber::ptr &subscriber)
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    subscribers.insert(subscriber);
                }
                // 取消订阅 或者 订阅者连接断开 的时候调用
                void removeSubscriber(const Subscriber::ptr &subscriber)
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    subscribers.erase(subscriber);
                }
                // 收到消息发布请求的时候调用
                void pushMessage(const BaseMessage::ptr &msg)
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    for (auto sub : subscribers)
                    {
                        sub->conn->send(msg);
                    }
                }
            };

        private:
            std::mutex _mutex;
            std::unordered_map<std::string, Topic::ptr> _topics;                   // 管理主题
            std::unordered_map<BaseConnection::ptr, Subscriber::ptr> _subscribers; // 管理订阅者(每一个都是独立的订阅者)
        };
    }
}
