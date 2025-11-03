#pragma once
#include "../common/net.hpp"
#include <set>
#include <unordered_map>

namespace TrRpc
{
    namespace server
    {
        class ProviderManager
        {
        public:
            using ptr = std::shared_ptr<ProviderManager>;
            struct Provider
            {
                using ptr = std::shared_ptr<Provider>;
                Provider(const BaseConnection::ptr &c, const Address &h)
                    : conn(c), host(h) {}
                BaseConnection::ptr conn;

                Address host;                     // 主机
                std::vector<std::string> methods; // 提供的方法
                std::mutex _mutex;                // 对 methods 操作的时候有线程安全问题
                void addmethod(std::string method)
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    methods.emplace_back(method);
                }
            };
            // 有新的服务注册: 1. 这个 conn 不存在;   2. conn存在 --> 这个主机提供的服务增多了
            void addProvider(const BaseConnection::ptr &conn, const Address &host, const std::string &method)
            {
                Provider::ptr provider;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _conns.find(conn);
                    if (it == _conns.end())
                    {
                        // 如果是新的: 构建 provider 对象, 添加到 _conns中
                        provider = std::make_shared<Provider>(conn, host);
                        _conns.insert(std::make_pair(conn, provider));
                    }
                    else // 旧的直接获取
                        provider = it->second;
                    // 对应方法多一个能提供的 主机(provider)
                    _providers[method].insert(provider);
                }
                // 对应的主机(provider)中添加它能提供的新方法
                provider->addmethod(method);
            }
            // 当一个服务提供者断开连接的时候，获取他的信息--用于进行服务下线通知
            Provider::ptr getProvider(const BaseConnection::ptr &conn)
            {
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _conns.find(conn);
                    if (it != _conns.end())
                        return it->second;
                    return Provider::ptr();
                }
            }
            // 当一个服务提供者断开连接的时候，删除它的关联信息
            void delProvider(const BaseConnection::ptr &conn)
            {
                Provider::ptr provider;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _conns.find(conn);
                    if (it == _conns.end())
                        return;
                    provider = it->second;
                    for (auto &method : provider->methods)
                    {
                        _providers[method].erase(provider);
                    }
                    _conns.erase(it);
                }
            }
            // 提供给客户端，用来返回方法的所有提供者的主机
            std::vector<Address> methodHosts(const std::string &method)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _providers.find(method);
                if (it == _providers.end())
                {
                    return std::vector<Address>();
                }
                std::vector<Address> result;
                for (auto &provider : it->second)
                {
                    result.push_back(provider->host);
                }
                return result;
            }

        private:
            std::mutex _mutex;
            // 用 set 是因为 set 支持快速查询, 比vector快
            // 对于一个方法，有哪些服务提供者能提供
            std::unordered_map<std::string, std::set<Provider::ptr>> _providers;
            // 一个连接对应的服务提供者是谁
            std::unordered_map<BaseConnection::ptr, Provider::ptr> _conns;
        };
        class DiscovererManager
        {
        public:
            using ptr = std::shared_ptr<DiscovererManager>;
            struct Discoverer
            {
                using ptr = std::shared_ptr<Discoverer>;
                Discoverer(BaseConnection::ptr c)
                    : conn(c) {}
                BaseConnection::ptr conn;
                std::vector<std::string> methods; // 发现过的服务
                std::mutex _mutex;
                void addmethod(std::string method)
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    methods.emplace_back(method);
                }
            };
            // 新增的服务发现者
            // 为什么是这个返回值 : Discoverer::ptr?
            Discoverer::ptr addDiscoverer(const BaseConnection::ptr &conn, const std::string method)
            {
                Discoverer::ptr discover;
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _conns.find(conn);
                    if (it == _conns.end())
                    {
                        discover = std::make_shared<Discoverer>(conn);
                        _conns.insert(std::make_pair(conn, discover));
                    }
                    else
                        discover = it->second;
                    _discoverers[method].insert(discover);
                }
                discover->addmethod(method);
                return discover;
            }
            // 发现者断开连接时，删除对应信息(_discoverers 和 _conns)
            void delDiscoverer(const BaseConnection::ptr conn)
            {
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _conns.find(conn);
                    if (it == _conns.end())
                        return;
                    auto discover = it->second;
                    for (auto &method : discover->methods)
                    {
                        _discoverers[method].erase(discover);
                    }
                    _conns.erase(it);
                }
            }
            // 新服务上线时，进行上线通知(通知的是发现者，所以在这个模块里面)
            void onlineNotify(const std::string &method, const Address &host)
            {
                return notify(method, host, ServiceOptype::SERVICE_ONLINE);
            }
            // 服务下线通知
            void offlineNotify(const std::string &method, const Address &host)
            {
                return notify(method, host, ServiceOptype::SERVICE_OFFLINE);
            }

        private:
            void notify(const std::string &method, const Address &host, ServiceOptype optype)
            {
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto it = _discoverers.find(method);
                    if (it == _discoverers.end())
                        return;
                    auto discovers = it->second; // 该方法对应的发现者们
                    auto msg_req = MessageFactory::create<ServiceRequest>();
                    msg_req->setId(UUid::uuid());
                    msg_req->setMtype(MType::REQ_SERVICE);
                    msg_req->setMethod(method);
                    msg_req->setHost(host);
                    msg_req->setOptype(optype);
                    for (auto &discover : discovers)
                    {
                        discover->conn->send(msg_req);
                    }
                }
            }
            std::mutex _mutex;
            // 这一个方法: 有多少发现者。因为当一个方法上线/下线的时候要通知对应的发现者
            std::unordered_map<std::string, std::set<Discoverer::ptr>> _discoverers;
            // 这个连接对应的发现者是谁
            std::unordered_map<BaseConnection::ptr, Discoverer::ptr> _conns;
        };
        // 服务注册中心
        // 整合上面两个模块 : 处理分布式系统中的服务注册、服务发现以及服务上下线通知等核心功能
        class PDManager
        {
        public:
            using ptr = std::shared_ptr<PDManager>;
            // 这是 PDManager 接收服务注册 / 服务发现请求的总入口
            // 所有客户端（服务提供者或消费者）发起的服务相关操作，都通过这个接口进入处理流程
            PDManager()
                : _providers(std::make_shared<ProviderManager>()),
                  _discoverers(std::make_shared<DiscovererManager>())
            {
            }
            void onServiceRequest(const BaseConnection::ptr conn, const ServiceRequest::ptr &svr_req)
            {
                // 服务操作请求：服务注册/服务发现/
                ServiceOptype optype = svr_req->optype();
                if (optype == ServiceOptype::SERVICE_REGISTRY)
                {
                    // 服务注册：
                    //  1. 新增服务提供者；  2. 进行服务上线的通知
                    INF_LOG("%s:%d 注册服务 %s", svr_req->host().first.c_str(), svr_req->host().second, svr_req->method().c_str());
                    _providers->addProvider(conn, svr_req->host(), svr_req->method());
                    _discoverers->onlineNotify(svr_req->method(), svr_req->host());
                    // 服务端还需要生成响应
                    return registryResponse(conn, svr_req);
                }
                else if (optype == ServiceOptype::SERVICE_DISCOVERY)
                {
                    // 服务发现：
                    //  1. 新增服务发现者
                    INF_LOG("客户端要进行 %s 服务发现！", svr_req->method().c_str());
                    _discoverers->addDiscoverer(conn, svr_req->method());
                    return discoveryResponse(conn, svr_req);
                }
                else
                {
                    ERR_LOG("收到服务操作请求，但是操作类型错误！");
                    return errorResponse(conn, svr_req);
                }
            }
            // 连接关闭事件接口
            // 当服务提供者或消费者的网络连接关闭时，会触发这个接口，进而执行后续的 “服务下线”“清理订阅” 逻辑
            void onConnShutdown(const BaseConnection::ptr &conn)
            {
                // 不知道是服务提供者的连接关闭了，还是服务发现者的连接关闭了
                // 并且: 一个服务有可能即是服务发现者又是服务提供者
                auto provider = _providers->getProvider(conn);
                if (provider != nullptr) // 代表是服务提供者的连接关闭了
                {
                    // 1. 服务下线通知   2. 删除服务提供者
                    INF_LOG("%s:%d 服务下线", provider->host.first.c_str(), provider->host.second);
                    for (auto &method : provider->methods)
                    {
                        _discoverers->offlineNotify(method, provider->host);
                    }
                    _providers->delProvider(conn);
                }
                _discoverers->delDiscoverer(conn);
            }

        private:
            void registryResponse(const BaseConnection::ptr conn, const ServiceRequest::ptr &svr_req)
            {
                auto svr_rsp = MessageFactory::create<ServiceResponse>();
                svr_rsp->setId(svr_req->rid());
                svr_rsp->setMtype(MType::RSP_SERVICE);
                svr_rsp->setRcode(RCode::RCODE_OK);
                svr_rsp->setOptype(ServiceOptype::SERVICE_REGISTRY);
                conn->send(svr_rsp);
            }

            void discoveryResponse(const BaseConnection::ptr conn, const ServiceRequest::ptr &svr_req)
            {
                auto svr_rsp = MessageFactory::create<ServiceResponse>();
                svr_rsp->setId(svr_req->rid());
                svr_rsp->setMtype(MType::RSP_SERVICE);
                svr_rsp->setOptype(ServiceOptype::SERVICE_DISCOVERY);
                std::vector<Address> hosts = _providers->methodHosts(svr_req->method());
                if (hosts.empty())
                {
                    svr_rsp->setRcode(RCode::RCODE_NOT_FOUND_SERVICE);
                    return conn->send(svr_rsp);
                }
                svr_rsp->setMethod(svr_req->method());
                svr_rsp->setHost(hosts);
                svr_rsp->setRcode(RCode::RCODE_OK);
                return conn->send(svr_rsp);
            }
            void errorResponse(const BaseConnection::ptr &conn, const ServiceRequest::ptr &svr_req)
            {
                auto svr_rsp = MessageFactory::create<ServiceResponse>();
                svr_rsp->setId(svr_req->rid());
                svr_rsp->setMtype(MType::RSP_SERVICE);
                svr_rsp->setRcode(RCode::RCODE_INVALID_OPTYPE);
                svr_rsp->setOptype(ServiceOptype::SERVICE_UNKNOW);
                conn->send(svr_rsp);
            }

        private:
            ProviderManager::ptr _providers;
            DiscovererManager::ptr _discoverers;
        };
    }
}