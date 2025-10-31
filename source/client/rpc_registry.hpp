#pragma once
#include "requestor.hpp"

namespace TrRpc
{
    namespace client
    {
        class Provider // 服务提供方: 需要给服务提供方法提供一个: 向服务器注册方法的借口
        {
        public:
            using ptr = std::shared_ptr<Provider>;
            Provider(const Requestor::ptr &requestor)
                : _requestor(requestor)
            {
            }
            bool serviceRegistry(const BaseConnection::ptr &conn, const std::string &method, const Address &host)
            {
                // 构建 "注册请求" 然后发给服务端
                ServiceRequest::ptr svr_req = MessageFactory::create<ServiceRequest>();
                svr_req->setId(UUid::uuid());
                svr_req->setMtype(MType::REQ_SERVICE);
                svr_req->setMethod(method);
                svr_req->setHost(host);
                svr_req->setOptype(ServiceOptype::SERVICE_REGISTRY);
                BaseMessage::ptr msg_rsp;
                bool ret = _requestor->send(conn, svr_req, msg_rsp);
                if (ret == false)
                {
                    ERR_LOG("%s 服务注册失败！", method.c_str());
                    return false;
                }
                auto svr_rsp = std::dynamic_pointer_cast<ServiceResponse>(msg_rsp);
                if (svr_rsp.get() == nullptr)
                {
                    ERR_LOG("响应类型向下转换失败！");
                    return false;
                }
                if (svr_rsp->rcode() != RCode::RCODE_OK)
                {
                    ERR_LOG("服务注册失败，原因：%s", errReason(svr_rsp->rcode()).c_str());
                    return false;
                }
                return true;
            }

        private:
            Requestor::ptr _requestor; // 发送请求需要用这个模块的特殊 send 接口
        };

        // 当获取一个服务的所有提供者的时候
        // 1. 我们将它保存起来  2. 采用 RR 轮转的方式进行访问(避免一个主机负载太大)
        class MethodHost // 用来描述一个方法 所有能提供该服务的主机
        {
        public:
            using ptr = std::shared_ptr<MethodHost>;
            MethodHost() : _idx(0) {}
            MethodHost(const std::vector<Address> &host)
                : _hosts(host.begin(), host.end()) {}

            void addHost(const Address &host)
            {
                // 中途收到了服务上线请求后被调用
                std::unique_lock<std::mutex> lock(_mutex);
                _hosts.emplace_back(host);
            }
            Address chooseHost()
            {
                std::unique_lock<std::mutex> lock(_mutex);
                int pos = _idx++ % _hosts.size();
                return _hosts[pos];
            }
            void removeHost(const Address &host)
            {
                // 中途收到了服务下线请求后被调用
                std::unique_lock<std::mutex> lock(_mutex);
                for (auto it = _hosts.begin(); it != _hosts.end(); it++)
                {
                    if (*it == host)
                    {
                        _hosts.erase(it);
                        break;
                    }
                }
            }
            bool empty() // 如果是空的要进行服务发现
            {
                std::unique_lock<std::mutex> lock(_mutex);
                return _hosts.empty();
            }

        private:
            std::mutex _mutex;
            int _idx; // 用于轮转
            std::vector<Address> _hosts;
        };
        class Discoverer
        {
        public:
            using OfflineCallback = std::function<void(const Address &)>;
            using ptr = std::shared_ptr<Discoverer>;
            Discoverer(Requestor::ptr requestor, const OfflineCallback &cb)
                : _requestor(requestor), _offline_callback(cb) {}
            // 客户端 conn 对 method 进行服务发现，host 是输出型参数, 客户端拿到host以后，通过host进行Rpc服务调用
            bool serviceDiscovery(const BaseConnection::ptr &conn, const std::string &method, Address &host)
            {
                // 如果有能提供服务的主机
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _method_hosts.find(method);
                    if (it != _method_hosts.end() && !it->second->empty())
                    {
                        host = it->second->chooseHost();
                        return true;
                    }
                }

                // 如果没有能提供服务的主机 --> 进行服务发现
                auto service_req = MessageFactory::create<ServiceRequest>();
                service_req->setId(UUid::uuid());
                service_req->setMethod(method);
                service_req->setMtype(MType::REQ_SERVICE);
                service_req->setOptype(ServiceOptype::SERVICE_DISCOVERY);
                auto msg_rsp = MessageFactory::create<BaseMessage>();
                bool ret = _requestor->send(conn, service_req, msg_rsp);
                if (ret == false)
                {
                    ERR_LOG("服务发现失败");
                    return false;
                }
                auto service_rsp = std::dynamic_pointer_cast<ServiceResponse>(msg_rsp);
                if (service_rsp->rcode() != RCode::RCODE_OK)
                {
                    ERR_LOG("服务发现失败, 错误原因: %s", errReason(service_rsp->rcode()));
                    return false;
                }
                if (service_rsp == nullptr)
                {
                    ERR_LOG("响应类型转换失败");
                    return false;
                }
                // 走到这里，一定是一开始没有能提供服务的主机，然后进行完了服务发现
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto hosts = std::make_shared<MethodHost>(service_rsp->hosts());
                    if (hosts->empty())
                    {
                        ERR_LOG("服务发现失败，没有可提供服务的主机");
                        return false;
                    }
                    _method_hosts[method] = hosts;
                    host = hosts->chooseHost();
                    return true;
                }
            }

            // 这个接口是提供给Dispatcher模块进行服务上线下线请求处理的 "回调函数"
            void onServiceRequest(const BaseConnection::ptr &conn, const ServiceRequest::ptr &msg)
            {
                // 1. 判断是上线/下线请求，如果都不是就不处理
                // 上线和下线请求是: 服务提供者的上线/下线
                auto optype = msg->optype();
                auto method = msg->method();
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    if (optype == ServiceOptype::SERVICE_ONLINE)
                    {
                        // 2. 上线请求：找到MethodHost，向其中新增一个主机地址

                        auto it = _method_hosts.find(method);
                        if (it == _method_hosts.end())
                        {
                            auto hosts = std::make_shared<MethodHost>();
                            hosts->addHost(msg->host());
                            _method_hosts[method] = hosts;
                        }
                        else
                            it->second->addHost(msg->host());
                    }
                    else if (optype == ServiceOptype::SERVICE_OFFLINE) // 服务提供者的下线通知
                    {
                        // 3. 下线请求：找到MethodHost，从其中删除一个主机地址
                        auto it = _method_hosts.find(method);
                        if (it == _method_hosts.end())
                            return;
                        it->second->removeHost(msg->host());
                        _offline_callback(msg->host());
                    }
                }
            }

        private:
            OfflineCallback _offline_callback;
            std::mutex _mutex;
            std::unordered_map<std::string, MethodHost::ptr> _method_hosts;
            Requestor::ptr _requestor;
        };

    }
}