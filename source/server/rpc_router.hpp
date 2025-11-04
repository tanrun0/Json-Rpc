#pragma once
#include "../common/net.hpp"
#include "../common/message.hpp"

namespace TrRpc
{
    namespace server
    {
        enum class VType
        {
            BOOL = 0,
            INTEGRAL,
            NUMERIC,
            STRING,
            ARRAY,
            OBJECT,
        };
        // 服务描述，一个服务一个服务描述(这个服务描述对象即代表: 服务)
        class ServiceDescribe
        {
        public:
            using ptr = std::shared_ptr<ServiceDescribe>;
            // 业务是以Json::Value的形式传进来的，返回值也会按 Json::Value 的形式组织返回
            using ServiceCallback = std::function<void(const Json::Value &, Json::Value &)>;
            using ParamsDescribe = std::pair<std::string, VType>;
            // 用建造者创建 ServiceDescribe的时候会构造好
            // 传右值避免不必要的拷贝
            ServiceDescribe(std::string &&mname, ServiceCallback &&cb,
                            std::vector<ParamsDescribe> &&params, VType rtype)
                : _method_name(std::move(mname)), _callback(std::move(cb)),
                  _params_desc(std::move(params)), _return_type(rtype)
            {
            }
            bool ParamCheck(const Json::Value &params) // 传入外界参数
            {
                for (auto &desc : _params_desc)
                {
                    // 遍历需要的参数, 查看params里面有没有
                    // 1. 确保有参数字段
                    if (params.isMember(desc.first) == false)
                    {
                        ERR_LOG("参数字段完整性校验失败！%s 字段缺失！", desc.first.c_str());
                        return false;
                    }
                    // 2. 确保类型要对
                    if (checkType(desc.second, params[desc.first]) == false)
                    {
                        ERR_LOG("%s 参数类型校验失败！", desc.first.c_str());
                        return false;
                    }
                }
                return true;
            }
            std::string method()
            {
                return _method_name;
            }
            // 业务处理函数
            bool Call(const Json::Value &params, Json::Value &result)
            {
                _callback(params, result);
                if (checkType(_return_type, result) == false)
                {
                    ERR_LOG("Rpc请求回调处理函数中, 返回值类型错误");
                    return false;
                }
                return true;
            }

        private:
            bool checkType(VType vtype, const Json::Value &val)
            {
                switch (vtype)
                {
                case VType::BOOL:
                    return val.isBool();
                case VType::INTEGRAL:
                    return val.isIntegral();
                case VType::NUMERIC:
                    return val.isNumeric();
                case VType::STRING:
                    return val.isString();
                case VType::ARRAY:
                    return val.isArray();
                case VType::OBJECT:
                    return val.isObject();
                }
                return false;
            }

        private:
            std::string _method_name;                 // 方法名称
            ServiceCallback _callback;                // 方法的实际回调处理函数
            std::vector<ParamsDescribe> _params_desc; // 参数列表，包含每个参数的描述
            VType _return_type;                       // 结果作为返回值类型的描述
        };
        // 建造者模式，通过建造者来初始化变量，建造好后无法更改
        class SDescribeFactory
        {
        public:
            using ptr = std::shared_ptr<SDescribeFactory>;
            void setMethodName(const std::string &name)
            {
                _method_name = name;
            }
            void setReturnType(VType vtype)
            {
                _return_type = vtype;
            }
            void setParamsDesc(const std::string &pname, VType vtype) // 设置单个?
            {
                _params_desc.push_back(ServiceDescribe::ParamsDescribe(pname, vtype));
            }
            void setCallback(const ServiceDescribe::ServiceCallback &cb)
            {
                _callback = cb;
            }
            ServiceDescribe::ptr build()
            {
                // ServiceDescribe ctor is (mname, callback, params, return_type)
                // ensure we pass callback before params
                return std::make_shared<ServiceDescribe>(std::move(_method_name), std::move(_callback),
                                                         std::move(_params_desc), _return_type);
            }

        private:
            std::string _method_name;                                  // 方法名称
            std::vector<ServiceDescribe::ParamsDescribe> _params_desc; // 参数列表，包含每个参数的描述
            ServiceDescribe::ServiceCallback _callback;                // 方法的实际回调处理函数
            VType _return_type;                                        // 结果作为返回值类型的描述
        };
        // 服务管理(真正管理服务描述的)
        class ServiceManager
        {
        public:
            using ptr = std::shared_ptr<ServiceManager>;
            void insert(ServiceDescribe::ptr desc)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _services.insert(std::make_pair(desc->method(), desc));
            }
            // 查询服务
            ServiceDescribe::ptr select(const std::string &method_name)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _services.find(method_name);
                if (it == _services.end())
                {
                    return ServiceDescribe::ptr();
                }
                return it->second;
            }
            // 删除服务
            void remove(const std::string &method_name)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _services.find(method_name);
                if (it == _services.end())
                {
                    return;
                }
                _services.erase(it);
            }

        private:
            std::mutex _mutex;
            std::unordered_map<std::string, ServiceDescribe::ptr> _services;
        };
        // 也是一张映射表，但是不单单是映射到回调函数上。
        // 我们收到一个 Request 的时候，要根据里面的 方法名 映射
        // 同时还需要解析参数，确保参数匹配，还可以确保返回值匹配
        // 因此：方法名,参数列表, 回调接口, 返回值。带有多成员，我们也可以封装成一个类(服务描述)
        // 建立根据方法名，直接到一整个 服务描述的映射，通过服务描述类完成：参数校验，回调等功能
        class RpcRouter
        {
        public:
            using ptr = std::shared_ptr<RpcRouter>;
            RpcRouter()
                : _service_manager(std::make_shared<ServiceManager>())
            {
            }
            // 这是设置给 Dispatcher 模块的针对 Rpc 请求进行回调处理的业务函数
            void onRpcRequest(BaseConnection::ptr &conn, RpcRequest::ptr &req)
            {
                // 1. 根据请求名称查找请求方法
                ServiceDescribe::ptr desc = _service_manager->select(req->method());
                if (desc.get() == nullptr)
                {
                    ERR_LOG("%s 服务未找到！", req->method().c_str());
                    return response(conn, req, Json::Value(), RCode::RCODE_NOT_FOUND_SERVICE);
                }
                // 2. 提取参数，进行参数校验
                Json::Value pramas = req->params();
                if (desc->ParamCheck(pramas) == false)
                {
                    ERR_LOG("%s 参数校验不成功", req->method().c_str());
                    return response(conn, req, Json::Value(), RCode::RCODE_INVALID_PARAMS);
                }
                // 3. (通过表里映射)调用具体业务处理函数处理
                Json::Value result;
                int ret = desc->Call(pramas, result);
                // 4. 得到结果，组织响应，向客户端发送
                if (ret == false)
                {
                    ERR_LOG("%s 服务回调出错", req->method().c_str());
                    return response(conn, req, Json::Value(), RCode::RCODE_INTERNAL_ERROR);
                }
                return response(conn, req, result, RCode::RCODE_OK);
            }
            // 注册服务方法
            void regeisterMethod(ServiceDescribe::ptr service)
            {
                _service_manager->insert(service);
            }

        private:
            // 根据结果组织响应 + 发送给客户端
            void response(const BaseConnection::ptr &conn,
                          const RpcRequest::ptr &req,
                          const Json::Value &result, RCode rcode)
            {
                auto rsp = MessageFactory::create<RpcResponse>();
                rsp->setId(req->rid());
                rsp->setMtype(MType::RSP_RPC);
                rsp->setRcode(rcode);
                rsp->setResult(result);
                conn->send(rsp);
            }

        private:
            ServiceManager::ptr _service_manager;
        };
    }
}
