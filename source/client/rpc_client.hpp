#pragma once
#include "requestor.hpp"
#include "rpcaller.hpp"
#include "rpc_registry.hpp"
#include "../common/dispatcher.hpp"

// 对 Rpc 业务客户端进行封装
// 1. 服务注册客户端: 让服务提供者可以向服务中心进行注册服务
// 2. 服务发现客户端：让服务发现者可以向服务中心进行服务发现
// 3. Rpc调用客户端：让 Rpc 调用者能够使用 Rpc 调用 : 3.1 不知道对方主机的调用（需要先服务发现） 3.2 知道对方主机的调用（直接调用）
namespace TrRpc
{
    namespace client
    {
        class RegistryClient
        {
        public:
            using ptr = std::shared_ptr<RegistryClient>;
            // 传入注册中心信息, 连接注册中心
            RegistryClient(const std::string &ip, int port)
                : _requestor(std::make_shared<Requestor>()),
                  _provider(std::make_shared<Provider>(_requestor)),
                  _dispatcher(std::make_shared<Dispatcher>())
            {
                auto rsp_cb = std::bind(&Requestor::onResponse, _requestor.get(), std::placeholders::_1, std::placeholders::_2);
                _dispatcher->registerHandler<BaseMessage>(MType::RSP_SERVICE, rsp_cb);
                auto msg_cb = std::bind(&Dispatcher::OnMessage, _dispatcher.get(), std::placeholders::_1, std::placeholders::_2);
                _client = ClientFactory::create(ip, port);
                _client->SetMessageCallback(msg_cb);
                _client->connect();
            }
            // 向外提供服务注册接口
            bool serviceRegistry(const std::string &method, const Address &host)
            {
                return _provider->serviceRegistry(_client->connection(), method, host);
            }

        private:
            Requestor::ptr _requestor;
            Provider::ptr _provider;
            Dispatcher::ptr _dispatcher;
            BaseClient::ptr _client; // 里面包含 connnection
        };

        class DiscoveryClient
        {
        public:
            using ptr = std::shared_ptr<DiscoveryClient>;
            // 传入注册中心信息, 连接注册中心
            DiscoveryClient(const std::string &ip, int port, const Discoverer::OfflineCallback &cb)
                : _requestor(std::make_shared<Requestor>()),
                  _discoverer(std::make_shared<Discoverer>(_requestor, cb)),
                  _dispatcher(std::make_shared<Dispatcher>())
            {
                auto rsp_cb = std::bind(&Requestor::onResponse, _requestor.get(), std::placeholders::_1, std::placeholders::_2);
                _dispatcher->registerHandler<BaseMessage>(MType::RSP_SERVICE, rsp_cb);

                // 在服务发现的过程中，可能会收到上线/下线请求
                auto on_off_req_cb = std::bind(&Discoverer::onServiceRequest, _discoverer.get(), std::placeholders::_1, std::placeholders::_2);
                _dispatcher->registerHandler<ServiceRequest>(MType::REQ_SERVICE, on_off_req_cb);

                auto msg_cb = std::bind(&Dispatcher::OnMessage, _dispatcher.get(), std::placeholders::_1, std::placeholders::_2);
                _client = ClientFactory::create(ip, port);
                _client->SetMessageCallback(msg_cb);
                _client->connect();
            }

            bool serviceDiscovery(const std::string &method, Address &host)
            {
                return _discoverer->serviceDiscovery(_client->connection(), method, host);
            }

        private:
            Requestor::ptr _requestor;
            Discoverer::ptr _discoverer;
            Dispatcher::ptr _dispatcher;
            BaseClient::ptr _client; // 里面包含 connnection
        };
        class RpcClient
        {
        public:
            using ptr = std::shared_ptr<RpcClient>;

            RpcClient(bool enableDiscovery, const std::string &ip, int port)
                : _enableDiscovery(enableDiscovery), _requestor(std::make_shared<Requestor>()),
                  _caller(std::make_shared<RpcCaller>(_requestor)), _dispatcher(std::make_shared<Dispatcher>())
            {
                // 对于 rpc_client 只会收到rpc_req
                auto rpc_rsp_cb = std::bind(&Requestor::onResponse, _requestor.get(), std::placeholders::_1, std::placeholders::_2);
                _dispatcher->registerHandler<BaseMessage>(MType::RSP_RPC, rpc_rsp_cb);

                // enableDiscovery--是否启用服务发现功能(Rpc 调用的两种情况)
                // 乳沟启用了服务发现，则地址信息是注册中心的地址，是服务发现客户端(_discoverer_client)需要连接的地址
                // 如果没有启用服务发现，则地址信息是服务提供者的地址，可以直接用来实例化(rpc_client)
                if (enableDiscovery)
                {
                    auto offline_cb = std::bind(&RpcClient::delClient, this, std::placeholders::_1);
                    _discovery_client = std::make_shared<DiscoveryClient>(ip, port, offline_cb);
                }
                else
                {
                    _rpc_client = ClientFactory::create(ip, port);
                    auto rpc_rsp_cb = std::bind(&Dispatcher::OnMessage, _dispatcher.get(), std::placeholders::_1, std::placeholders::_2);
                    _rpc_client->SetMessageCallback(rpc_rsp_cb);
                    _rpc_client->connect();
                }
            }

            // 三种不同的调用方式
            bool call(const std::string &method, const Json::Value &params, Json::Value &result)
            {
                // 获取服务提供者：1. 服务发现；  2. 固定服务提供者
                BaseClient::ptr client = getRpcClient(method);
                if (client.get() == nullptr)
                {
                    return false;
                }
                // 3. 通过客户端连接，发送rpc请求
                return _caller->call(client->connection(), method, params, result);
            }
            bool call(const std::string &method, const Json::Value &params, RpcCaller::JsonAsyncResponse &result)
            {
                BaseClient::ptr client = getRpcClient(method);
                if (client.get() == nullptr)
                {
                    return false;
                }
                // 3. 通过客户端连接，发送rpc请求
                return _caller->call(client->connection(), method, params, result);
            }
            bool call(const std::string &method, const Json::Value &params, const RpcCaller::JsonResponseCallback &cb)
            {
                BaseClient::ptr client = getRpcClient(method);
                if (client.get() == nullptr)
                {
                    return false;
                }
                // 3. 通过客户端连接，发送rpc请求
                return _caller->call(client->connection(), method, params, cb);
            }

        private:
            // 下面针对的都是 : 从 DiscoveryClient 得到的 客户端连接, 用于维护客户端连接池
            BaseClient::ptr newClient(const Address &host)
            {
                // 建立和服务提供主机有连接的client
                auto msg_cb = std::bind(&Dispatcher::OnMessage, _dispatcher.get(), std::placeholders::_1, std::placeholders::_2);
                auto client = ClientFactory::create(host.first, host.second);
                client->SetMessageCallback(msg_cb);
                client->connect();
                putClient(host, client);
                return client;
            }
            void putClient(const Address &host, BaseClient::ptr &client)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _rpc_clients.insert(std::make_pair(host, client));
            }
            BaseClient::ptr getClient(const Address &host)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _rpc_clients.find(host);
                if (it == _rpc_clients.end())
                {
                    return BaseClient::ptr();
                }
                return it->second;
            }
            void delClient(const Address &host)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _rpc_clients.erase(host);
            }
            // 获取 RpcCLient 的真正接口，内部判断是否: 通过服务发现者，要从池里面拿
            BaseClient::ptr getRpcClient(const std::string &method)
            {
                BaseClient::ptr client;
                if (_enableDiscovery)
                {
                    // 1. 通过服务发现，获取服务提供者地址信息
                    Address host;
                    bool ret = _discovery_client->serviceDiscovery(method, host); // RR 轮转获得服务提供者 host(输出型参数)
                    if (ret == false)
                    {
                        ERR_LOG("当前 %s 服务，没有找到服务提供者！", method.c_str());
                        return BaseClient::ptr();
                    }
                    // 2. 查看服务提供者是否已有实例化客户端，有则直接使用，没有则创建
                    client = getClient(host);
                    if (client.get() == nullptr)
                    { // 没有找打已实例化的客户端，则创建
                        client = newClient(host);
                    }
                }
                else
                {
                    client = _rpc_client;
                }
                return client;
            }

        private:
            // 哈希表的 键 要求: 能够计算哈希值，对于自定义类型，我们需要使用 hash 方法来提供计算哈希值的仿函数
            struct AddressHash
            {
                size_t operator()(const Address &host) const
                {
                    std::string addr = host.first + std::to_string(host.second);
                    return std::hash<std::string>{}(addr);
                }
            };
            bool _enableDiscovery;
            DiscoveryClient::ptr _discovery_client; // 启动了服务发现，需要用到的服务发现客户端
            Requestor::ptr _requestor;
            RpcCaller::ptr _caller;
            Dispatcher::ptr _dispatcher;
            BaseClient::ptr _rpc_client; // 用于未启用服务发现
            std::mutex _mutex;
            //<"127.0.0.1:8080", client1>
            // 长连接: 我们获得一个主机的时候，先看看连接池里面有没有对应的客户端连接可以复用
            std::unordered_map<Address, BaseClient::ptr, AddressHash> _rpc_clients; // 用于服务发现的客户端连接池
        };

    }
}