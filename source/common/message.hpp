#pragma once
#include "detail.hpp"
#include "abstract.hpp"

// 根据不同的需求，对 Message 进行实现

namespace TrRpc
{
    typedef std::pair<std::string, int> Address; // 主机地址(ip, port)
    // 在这里多设计一个 JsonMessage 作为父类，代表 Json类消息格式
    // 避免下面的 Request 和 Response（它们是在特定业务场景下的消息），不过进一步进行了细分
    class JsonMessage : public BaseMessage
    {
    public:
        using ptr = std::shared_ptr<JsonMessage>;
        virtual std::string serialize() override
        {
            std::string str;
            bool ret = JsonUtil::Serialize(_body, &str);
            if (ret == false)
                return std::string();
            return str;
        }
        virtual bool deserialize(const std::string &msg) override
        {
            return JsonUtil::DeSerialize(msg, &_body);
        }

    protected:
        Json::Value _body; // 存储消息的核心业务数据
    };

    // 明确 “请求类消息” 的抽象层次，请求类消息继承这个
    class JsonRequest : public JsonMessage
    {
    public:
        using ptr = std::shared_ptr<JsonRequest>;
    };
    class JsonResponse : public JsonMessage
    {
    public:
        using ptr = std::shared_ptr<JsonResponse>;
        virtual bool check() override
        {
            // 在响应中，大部分的响应都只有响应状态码
            // 因此只需要判断响应状态码字段是否存在，类型是否正确即可
            if (_body[KEY_RCODE].isNull() || !_body[KEY_RCODE].isIntegral())
            {
                ERR_LOG("响应中没有响应状态码 或 响应状态码类型错误");
                return false;
            }
            return true;
        }
        RCode rcode() // 把相同的接口实现在父类
        {
            return (RCode)_body[KEY_RCODE].asInt();
        }
        void setRcode(RCode rcode)
        {
            _body[KEY_RCODE] = (int)rcode;
        }
    };

    // 业务 1: RPC请求
    // 需要: 客户端：设置方法和参数(核心业务数据) --> 序列化放到正文中(以便发送)
    //       服务端: 收到正文: 反序列化得到核心业务数据(_body) -->  check判断请求是否合法(只看结构) --> 获取方法和参数
    class RpcRequest : public JsonRequest
    {
    public:
        using ptr = std::shared_ptr<RpcRequest>;
        virtual bool check() override
        {
            if (_body[KEY_METHOD].isNull() || !_body[KEY_METHOD].isString())
            {
                ERR_LOG("Rpc 请求中: 方法不存在 或 方法类型错误");
                return false;
            }
            if (_body[KEY_PARAMS].isNull() || !_body[KEY_PARAMS].isObject()) // 参数也是用一个Json::Value对象存储的
            {
                ERR_LOG("Rpc 请求中: 参数不存在 或 参数类型错误");
                return false;
            }
            return true; // 代表请求消息合法
        }
        std::string method()
        {
            return _body[KEY_METHOD].asString();
        }
        void setMethod(const std::string &method_name)
        {
            _body[KEY_METHOD] = method_name;
        }
        Json::Value params()
        {
            return _body[KEY_PARAMS];
        }
        void setParams(const Json::Value &params)
        {
            _body[KEY_PARAMS] = params;
        }
    };

    class RpcResponse : public JsonResponse
    {
    public:
        using ptr = std::shared_ptr<RpcResponse>;
        virtual bool check() override
        {
            if (_body[KEY_RCODE].isNull() || !_body[KEY_RCODE].isIntegral())
            {
                ERR_LOG("Rpc响应中: 没有响应状态码 或 响应状态码类型错误");
                return false;
            }
            if (_body[KEY_RESULT].isNull() || !_body[KEY_RESULT].isObject()) // 不管什么类型的结果都放在Json::Value里面吧
            {
                ERR_LOG("Rpc响应中: 没有结果 或 结果类型错误");
                return false;
            }
            return true;
        }
        Json::Value result()
        {
            return _body[KEY_RESULT];
        }
        void setResult(const Json::Value &result)
        {
            _body[KEY_RESULT] = result;
        }
    };

    // 业务 2: 主题发布和订阅
    class TopicRequest : public JsonRequest
    {
    public:
        using ptr = std::shared_ptr<TopicRequest>;
        virtual bool check() override
        {
            if (_body[KEY_TOPIC_KEY].isNull() || !_body[KEY_TOPIC_KEY].isString())
            {
                ERR_LOG("主题请求中: 主题不存在 或 主题类型错误");
                return false;
            }
            if (_body[KEY_OPTYPE].asInt() == (int)TopicOptype::TOPIC_PUBLISH && (_body[KEY_TOPIC_MSG].isNull() || !_body[KEY_TOPIC_MSG].isString()))
            {
                ERR_LOG("消息发布给主题请求中: 消息不存在 或 消息类型错误");
                return false;
            }
            if (_body[KEY_OPTYPE].isNull() || !_body[KEY_OPTYPE].isIntegral())
            {
                ERR_LOG("主题请求中: 操作方法的类型 或 操作方法的类型错误");
                return false;
            }
            return true;
        }
        // 以下是对上面三个核心业务数据的设置(client)和获取(server)
        std::string topickey()
        {
            return _body[KEY_TOPIC_KEY].asString();
        }
        void setTopicKey(const std::string name) // 设置主题名称
        {
            _body[KEY_TOPIC_KEY] = name;
        }
        TopicOptype optype()
        {
            return (TopicOptype)_body[KEY_OPTYPE].asInt();
        }
        void setOptype(const TopicOptype &optype)
        {
            // 应为 Json 中只能存放基础类型，不能存放枚举类型
            _body[KEY_OPTYPE] = (int)optype;
        }
        std::string topicMsg()
        {
            return _body[KEY_TOPIC_MSG].asString();
        }
        void setTopicMsg(const std::string &msg)
        {
            _body[KEY_TOPIC_MSG] = msg;
        }
    };

    class TopicResponse : public JsonResponse
    {
    public:
        using ptr = std::shared_ptr<TopicResponse>;
        virtual bool check() override
        {
            if (_body[KEY_RCODE].isNull() || !_body[KEY_RCODE].isIntegral())
            {
                ERR_LOG("主题响应中: 没有响应状态码 或 响应状态码类型错误");
                return false;
            }
            return true;
        }
    };

    // 业务 3: 服务注册与发现
    class ServiceRequest : public JsonRequest
    {
    public:
        using ptr = std::shared_ptr<ServiceRequest>;
        virtual bool check() override
        {
            if (_body[KEY_METHOD].isNull() || !_body[KEY_METHOD].isString())
            {
                ERR_LOG("服务注册与发现请求中: 方法不存在 或 方法类型错误");
                return false;
            }
            if (_body[KEY_OPTYPE].isNull() || !_body[KEY_OPTYPE].isIntegral())
            {
                ERR_LOG("服务注册与发现请求中: 操作方法的类型 或 操作方法的类型错误");
                return false;
            }
            // 如果操作类型不是: 客户端发现，必须严格校验 host 字段的合法性 （客户端发现的请求中无需 host 字段）
            if (_body[KEY_OPTYPE].asInt() != (int)(ServiceOptype::SERVICE_DISCOVERY) &&
                (_body[KEY_HOST].isNull() == true ||                    // host 字段不存在
                 _body[KEY_HOST].isObject() == false ||                 // host 不是 JSON 对象
                 _body[KEY_HOST][KEY_HOST_IP].isNull() == true ||       // host.ip 不存在
                 _body[KEY_HOST][KEY_HOST_IP].isString() == false ||    // host.ip 不是字符串
                 _body[KEY_HOST][KEY_HOST_PORT].isNull() == true ||     // host.port 不存在
                 _body[KEY_HOST][KEY_HOST_PORT].isIntegral() == false)) // host.port 不是整数
            {
                ERR_LOG("服务注册与发现请求中: 主机地址信息错误！");
                return false;
            }
            return true;
        }
        // 提供各种字段的设置和获取接口
        std::string method()
        {
            return _body[KEY_METHOD].asString();
        }
        void setMethod(const std::string &method_name)
        {
            _body[KEY_METHOD] = method_name;
        }
        ServiceOptype optype()
        {
            return (ServiceOptype)_body[KEY_OPTYPE].asInt();
        }
        void setOptype(const ServiceOptype &optype)
        {
            // 应为 Json 中只能存放基础类型，不能存放枚举类型
            _body[KEY_OPTYPE] = (int)optype;
        }
        Address host() // 注意： 虽然原来里面是 JSON::VALUE，但是我们这里直接获取出来(ip, port)
        {
            Address addr;
            addr.first = _body[KEY_HOST][KEY_HOST_IP].asString();
            addr.second = _body[KEY_HOST][KEY_HOST_PORT].asInt();
            return addr;
        }
        void setHost(const Address &host)
        {
            Json::Value val;
            val[KEY_HOST_IP] = host.first;
            val[KEY_HOST_PORT] = host.second;
            _body[KEY_HOST] = val;
        }
    };
    class ServiceResponse : public JsonResponse
    {
    public:
        using ptr = std::shared_ptr<ServiceResponse>;
        virtual bool check() override
        {
            if (_body[KEY_RCODE].isNull() || !_body[KEY_RCODE].isIntegral())
            {
                ERR_LOG("服务注册与发现响应中: 没有响应状态码 或 响应状态码类型错误");
                return false;
            }
            if (_body[KEY_OPTYPE].isNull() || !_body[KEY_OPTYPE].isIntegral())
            {
                ERR_LOG("服务注册与发现响应中: 没有操作类型 或 操作类型的类型错误！");
                return false;
            }
            // 服务发现的响应，需要填充 服务端的方法和主机
            if (_body[KEY_OPTYPE].asInt() == (int)(ServiceOptype::SERVICE_DISCOVERY) &&
                ((_body[KEY_METHOD].isNull() ||
                  !_body[KEY_METHOD].isString() ||
                  _body[KEY_HOST].isNull() ||
                  !_body[KEY_HOST].isArray())))
            {
                ERR_LOG("服务发现响应中响应信息字段错误！");
                return false;
            }
            return true;
        }
        ServiceOptype optype()
        {
            return (ServiceOptype)_body[KEY_OPTYPE].asInt();
        }
        void setOptype(ServiceOptype optype)
        {
            _body[KEY_OPTYPE] = (int)optype;
        }
        std::string method()
        {
            return _body[KEY_METHOD].asString();
        }
        void setMethod(const std::string &method)
        {
            _body[KEY_METHOD] = method;
        }
        // 这里的服务端主机要用数组，因为有可能多个主机都可以提供相同的服务
        std::vector<Address> hosts()
        {
            std::vector<Address> addrs;
            for (int i = 0; i < _body[KEY_HOST].size(); i++)
            {
                Address addr;
                addr.first = _body[KEY_HOST][i][KEY_HOST_IP].asString();
                addr.second = _body[KEY_HOST][i][KEY_HOST_PORT].asInt();
                addrs.push_back(addr);
            }
            return addrs;
        }
        void setHost(const std::vector<Address> &addrs)
        {
            for (const auto &addr : addrs)
            {
                Json::Value host;
                host[KEY_HOST_IP] = addr.first;
                host[KEY_HOST_PORT] = addr.second;
                _body[KEY_HOST].append(host);
            }
        }
    };
    // 设计一个消息对象的生产工厂(返回指向子类的基类指针)
    // 提供统一接口，避免一直 new 不同的消息对象
    class MessageFactory
    {
    public:
        // 根据消息类型枚举 MType 自动创建对应类型的消息对象
        // 不知道具体类型，仅知道消息类型时调用，返回父类的指针（即: 兼容所有子类，统一接口）
        static BaseMessage::ptr create(MType mtype)
        {
            switch (mtype)
            {
            case MType::REQ_RPC:
                return std::make_shared<RpcRequest>();
            case MType::RSP_RPC:
                return std::make_shared<RpcResponse>();
            case MType::REQ_TOPIC:
                return std::make_shared<TopicRequest>();
            case MType::RSP_TOPIC:
                return std::make_shared<TopicResponse>();
            case MType::REQ_SERVICE:
                return std::make_shared<ServiceRequest>();
            case MType::RSP_SERVICE:
                return std::make_shared<ServiceResponse>();
            }
            return BaseMessage::ptr();
        }
        // 右值引用(自动推导适应) + 完美转发(把参数保留原属性，向下传递去构造)
        template <typename T, typename... Args>
        static std::shared_ptr<T> create(Args &&...args)
        {
            return std::make_shared<T>(std::forward<Args>(args)...);
        }
    };
}