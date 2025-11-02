#pragma once
#include "../common/net.hpp"
#include "../common/message.hpp"
#include <unordered_map>
#include <future>
// 因为普通的send以后，响应到达的顺序是不一定的，不知道响应要交给谁，所以我们可以借助 ID (以下还添加获取响应的其他方式)
// 当我们 send一个请求以后，会生成对应的 RequestDesc
// 收到响应时，根据响应的 ID，把响应分发给对应的 RequestDesc
// 设置了send以后三种不同的获取响应(BaseMessage)的规则:
// 1. 异步获取响应(返回 future, 以后自己get())
// 2. 同步阻塞获取响应(发送请求后, 直到获取响应了才返回)
// 3. 回调处理响应(无须主动获取，把响应传给回调去处理)

namespace TrRpc
{
    namespace client
    {

        class Requestor
        {
        public:
            using ptr = std::shared_ptr<Requestor>;
            using RequestCallback = std::function<void(const BaseMessage::ptr)>; // 处理响应的回调
            using AsyncResponse = std::future<BaseMessage::ptr>;                 // 存放响应，支持异步获取
            struct RequestDesc
            {
                using ptr = std::shared_ptr<RequestDesc>;

                BaseMessage::ptr request;
                RType rtype;                             // 标记请求规则
                std::promise<BaseMessage::ptr> response; // 存放响应，后续通过 future 支持异步获取
                // 回调函数(给回调处理提供)
                RequestCallback calllback;
            };
            // 提供给底层 Connection 的回调设置: 收到响应后进行响应处理
            void onResponse(const BaseConnection::ptr &conn, BaseMessage::ptr &msg)
            {
                std::string rid = msg->rid();
                RequestDesc::ptr rdp = getDescribe(rid);
                if (rdp.get() == nullptr)
                {
                    ERR_LOG("收到响应, 但请求描述不存在");
                    return;
                }
                if (rdp->rtype == RType::REQ_ASYNC) // 根据请求处理规则，分发响应
                    rdp->response.set_value(msg);
                else if (rdp->rtype == RType::REQ_CALLBACK && rdp->calllback)
                    rdp->calllback(msg);
                else
                    ERR_LOG("请求处理规则未知");
                delDescribe(rid); // 删除处理完的请求
            }
            // 设置特殊的send接口给上层用, 响应获取方式分三种:
            // 发送请求，并且希望异步获取响应
            bool send(const BaseConnection::ptr &conn, const BaseMessage::ptr &req, AsyncResponse &async_rsp)
            {
                RequestDesc::ptr rdp = newDescribe(req, RType::REQ_ASYNC);
                if (rdp.get() == nullptr)
                {
                    ERR_LOG("构造请求对象失败");
                    return false;
                }
                conn->send(req);
                async_rsp = rdp->response.get_future();
                return true;
            }
            // 同步获取响应
            bool send(const BaseConnection::ptr &conn, const BaseMessage::ptr &req, BaseMessage::ptr &rsp)
            {
                AsyncResponse req_future;
                bool ret = send(conn, req, req_future);
                if (ret == false)
                    return false;
                rsp = req_future.get();
                return true;
            }
            // 回调处理响应
            bool send(const BaseConnection::ptr &conn, const BaseMessage::ptr &req, RequestCallback &cb)
            {
                RequestDesc::ptr rdp = newDescribe(req, RType::REQ_CALLBACK, cb);
                if (rdp.get() == nullptr)
                {
                    ERR_LOG("构造请求对象失败");
                    return false;
                }
                conn->send(req); // send 直接发出去，对面收到了调用 OnResponse 直接把响应回调处理了，我们无须关心获取响应
                return true;
            }

        private:
            RequestDesc::ptr newDescribe(const BaseMessage::ptr &req, RType rt, const RequestCallback &cb = RequestCallback())
            {
                std::unique_lock<std::mutex> lock(_mutex);
                RequestDesc::ptr desc = std::make_shared<RequestDesc>();
                desc->request = req;
                desc->rtype = rt;
                if (rt == RType::REQ_CALLBACK && cb)
                    desc->calllback = cb;
                _request_desc.insert(std::make_pair(req->rid(), desc));
                return desc;
            }
            RequestDesc::ptr getDescribe(const std::string &rid)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                auto it = _request_desc.find(rid);
                if (it == _request_desc.end())
                {
                    return RequestDesc::ptr();
                }
                return it->second;
            }
            void delDescribe(const std::string &rid)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _request_desc.erase(rid);
            }

        private:
            std::unordered_map<std::string, RequestDesc::ptr> _request_desc;
            std::mutex _mutex;
        };
    }

}