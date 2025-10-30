#pragma once
#include "../common/net.hpp"
#include "../common/message.hpp"
#include <unordered_map>
#include "requestor.hpp"
#include <future>

namespace TrRpc
{
    namespace client
    {
        // 向用户提供 Rpc 调用的模块, 向外提供⼏个rpc调⽤的接⼝，内部实现向服务端发送请求，响应的获取方式由请求处理规则决定(同 or 异)
        // 把要发送的请求传给rpccaller模块，让rpccaller模块去帮忙调用
        class RpcCaller
        {
        public:
            using ptr = std::shared_ptr<RpcCaller>;
            using JsonAsyncResponse = std::future<Json::Value>;                    // 异步调用的返回结果
            using JsonResponseCallback = std::function<void(const Json::Value &)>; // 异步回调的函数类型
            // 传入的原因是：让多个rpccaller 共用一个 requestor （请求管理模块），没必要为每个caller都创建新的
            RpcCaller(const Requestor::ptr req) : _requestor(req) {}

            // 异步调用
            bool call(const BaseConnection::ptr &conn, const std::string &method, const Json::Value &params, JsonAsyncResponse &result)
            {
                // 1. 组织请求
                auto req = MessageFactory::create<RpcRequest>();
                req->setId(UUid::uuid());
                req->setMethod(method);
                req->setMtype(MType::REQ_RPC);
                req->setParams(params);
                // 因为异步回调拿到的是一个 BaseMessage 响应，不是 Jason::Value
                // 所以我们可以选择: 回调处理响应，传入一个promise来存储响应里面的result，然后出来再获取它
                auto json_promise = std::make_shared<std::promise<Json::Value>>();
                Requestor::RequestCallback cb = std::bind(&RpcCaller::Callback, this, json_promise, std::placeholders::_1);
                bool ret = _requestor->send(conn, req, cb); // 上面auto推导的话，下面这个bind不行，因为 参数类型是function的可调用对象，上面是bind。显式以后会发生隐式类型转换
                if (ret == false)
                {
                    ERR_LOG("异步Rpc请求失败! ");
                    return false;
                }
                return true;
            }
            // 同步调用
            bool call(const BaseConnection::ptr &conn, const std::string &method, const Json::Value &params, Json::Value &result)
            {
                // 1. 组织请求
                auto req = MessageFactory::create<RpcRequest>();
                req->setId(UUid::uuid());
                req->setMethod(method);
                req->setMtype(MType::REQ_RPC);
                req->setParams(params);
                BaseMessage::ptr rsp_msg; // 存放同步调用的应答
                // 2. 发送同步请求
                bool ret = _requestor->send(conn, req, rsp_msg);
                if (ret == false)
                {
                    ERR_LOG("发送同步 Rpc 请求失败");
                    return false;
                }
                // 3. 获取响应, 并设置 RpcResponse里边的result
                auto rpc_rsp_msg = std::dynamic_pointer_cast<RpcResponse>(rsp_msg);
                if (rpc_rsp_msg.get() == nullptr)
                {
                    ERR_LOG("rpc响应, 向下类型转换失败");
                    return false;
                }
                if (rpc_rsp_msg->rcode() != RCode::RCODE_OK)
                {
                    ERR_LOG("rpc请求出错: %s", errReason(rpc_rsp_msg->rcode()).c_str());
                    return false;
                }
                result = rpc_rsp_msg->result();
                return true;
            }
            // 异步回调
            bool call(const BaseConnection::ptr &conn, const std::string &method, const Json::Value &params, const JsonResponseCallback &cb)
            {
                // 该层(参数传入的)回调是针对结果处理，底层(requestor->send的)回调是针对响应 BaseMessage
                // 所以我们想让本层的回调被调用，就需要构造一个 针对BaseMessage 的回调，然后在里面调用用户的 cb
                // 1. 组织请求
                auto req = MessageFactory::create<RpcRequest>();
                req->setId(UUid::uuid());
                req->setMethod(method);
                req->setMtype(MType::REQ_RPC);
                req->setParams(params);

                // 2. 发送请求
                Requestor::RequestCallback req_cb = std::bind(&RpcCaller::Callback2, this, cb, std::placeholders::_1);
                int ret = _requestor->send(conn, req, req_cb);
                if (ret == false)
                {
                    ERR_LOG("发送异步回调 Rpc请求错误");
                    return false;
                }
                // 不需要获取响应了，因为是结果回调处理
                return true;
            }

        private:
            void Callback(std::shared_ptr<std::promise<Json::Value>> result, const BaseMessage::ptr &rsp_msg)
            {
                auto rpc_rsp_msg = std::dynamic_pointer_cast<RpcResponse>(rsp_msg);
                if (!rpc_rsp_msg)
                {
                    ERR_LOG("rpc响应, 向下类型转换失败！");
                    return;
                }
                if (rpc_rsp_msg->rcode() != RCode::RCODE_OK)
                {
                    ERR_LOG("rpc异步请求出错: %s", errReason(rpc_rsp_msg->rcode()).c_str());
                    return;
                }
                result->set_value(rpc_rsp_msg->result());
            }
            //
            void Callback2(JsonResponseCallback &cb, const BaseMessage::ptr &rsp_msg)
            {
                auto rpc_rsp_msg = std::dynamic_pointer_cast<RpcResponse>(rsp_msg);
                if (rpc_rsp_msg == nullptr)
                {
                    ERR_LOG("rpc响应, 向下类型转换失败");
                    return;
                }
                if (rpc_rsp_msg->rcode() != RCode::RCODE_OK)
                {
                    ERR_LOG("rpc异步请求出错: %s", errReason(rpc_rsp_msg->rcode()).c_str());
                    return;
                }
                if (cb)
                    cb(rpc_rsp_msg->result());
            }

        private:
            Requestor::ptr _requestor;
        };
    }

}
