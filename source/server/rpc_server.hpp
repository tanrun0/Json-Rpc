#pragma once
#include "../common/net.hpp"
#include "../common/dispatcher.hpp"
#include "rpc_registry.hpp"
#include "../client/rpc_client.hpp"
#include "rpc_router.hpp"
#include "rpc_topic.hpp"

namespace TrRpc
{
    namespace server
    {
        // 注册中心服务端: 处理 服务注册 / 服务发现 的请求
        // 就一个启动功能，启动完以后就坐等接受请求
        class RegistryServer
        {
        public:
            using ptr = std::shared_ptr<RegistryServer>;
            RegistryServer(int port)
                : _pd_manager(std::make_shared<PDManager>()),
                  _dispatcher(std::make_shared<Dispatcher>()),
                  _server(ServerFactory::create(port))
            {
                // 设置分发模块的回调(针对收到的服务请求, 在回调函数内部自行判断是注册请求还是发现请求)
                auto service_cb = std::bind(&PDManager::onServiceRequest, _pd_manager.get(), std::placeholders::_1, std::placeholders::_2);
                _dispatcher->registerHandler<ServiceRequest>(MType::REQ_SERVICE, service_cb);
                // 设置底层网络服务端的消息回调(把分发器设置给底层)
                auto message_cb = std::bind(&Dispatcher::OnMessage, _dispatcher.get(), std::placeholders::_1, std::placeholders::_2);
                _server->SetMessageCallback(message_cb);
                // 设置关闭回调(调用 pd_manager 的 shutdown, 把该关的关了，管理好自己的成员)
                auto close_cb = std::bind(&PDManager::onConnShutdown, _pd_manager.get(), std::placeholders::_1);
                _server->SetCloseCallback(close_cb);
            }
            void Start()
            {
                _server->start();
            }

        private:
            PDManager::ptr _pd_manager;  // 注册中心，处理业务的模块
            Dispatcher::ptr _dispatcher; // 分发模块，需要给底层网络层设置回调的
            BaseServer::ptr _server;     // 网络服务端
        };

        // 提供 Rpc 方法的注册
        // 需要内置一个 可用于rpc注册的客户端
        class RpcServer
        {
        public:
            using ptr = std::shared_ptr<RpcServer>;
            //  1. rpc服务提供端地址信息--必须是 rpc 服务器对外访问地址（云服务器---监听地址和访问地址不同）
            //  2. 注册中心服务端地址信息 -- 启用服务注册后，连接注册中心进行服务注册用的
            RpcServer(Address access_addr, Address reg_server_addr = Address(), bool enablediscover = false)
                : _access_addr(access_addr), _enableRegistry(enablediscover),
                  _dispatcher(std::make_shared<Dispatcher>()), _router(std::make_shared<RpcRouter>())

            {
                // 是否将方法注册到 注册中心，如果是: 则自己也是身为注册的客户端的
                if (_enableRegistry == true)
                    _reg_client = std::make_shared<client::RegistryClient>(reg_server_addr.first, reg_server_addr.second);
                // 当前成员server是一个rpcserver，用于提供rpc服务的
                auto rpc_cb = std::bind(&RpcRouter::onRpcRequest, _router.get(), std::placeholders::_1, std::placeholders::_2);
                _dispatcher->registerHandler<RpcRequest>(MType::REQ_RPC, rpc_cb);

                _server = ServerFactory::create(access_addr.second);
                auto message_cb = std::bind(&Dispatcher::OnMessage, _dispatcher.get(), std::placeholders::_1, std::placeholders::_2);
                _server->SetMessageCallback(message_cb);
            }
            void registerMethod(const ServiceDescribe::ptr &service)
            {
                if (_enableRegistry) // 方法注册到注册中心
                {
                    _reg_client->serviceRegistry(service->method(), _access_addr);
                }
                _router->regeisterMethod(service); // 方法注册到本地
            }
            void start()
            {
                _server->start();
            }

        private:
            Address _access_addr; // rpc服务器对外访问地址(云服务器)
            bool _enableRegistry; // Rpc服务器，是否将自己能提供的Rpc调用服务注册到 注册中心
            client::RegistryClient::ptr _reg_client;
            Dispatcher::ptr _dispatcher;
            RpcRouter::ptr _router;
            BaseServer::ptr _server;
        };

        // 主题业务 服务端
        class TopicServer
        {
        public:
            using ptr = std::shared_ptr<TopicServer>;
            TopicServer(int port)
                : _topic_manager(std::make_shared<TopicManager>()),
                  _dispatcher(std::make_shared<Dispatcher>()),
                  _server(ServerFactory::create(port))
            {
                auto topic_cb = std::bind(&TopicManager::onTopicRequest, _topic_manager.get(), std::placeholders::_1, std::placeholders::_2);
                _dispatcher->registerHandler<TopicRequest>(MType::REQ_TOPIC, topic_cb);
                auto message_cb = std::bind(&Dispatcher::OnMessage, _dispatcher.get(), std::placeholders::_1, std::placeholders::_2);
                _server->SetMessageCallback(message_cb);
                // 设置关闭回调(调用 pd_manager 的 shutdown, 把该关的关了，管理好自己的成员)
                auto close_cb = std::bind(&TopicManager::onShutdown, _topic_manager.get(), std::placeholders::_1);
                _server->SetCloseCallback(close_cb);
            }
            void Start()
            {
                _server->start();
            }

        private:
            TopicManager::ptr _topic_manager;
            Dispatcher::ptr _dispatcher;    
            BaseServer::ptr _server;        
        };
    }
}