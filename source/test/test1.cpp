#include "../common/message.hpp"

int main()
{
    // 测试 RpcRequest 和 RpcResponse
    // 模拟发送一个 Add 的 Rpc调用请求
    TrRpc::RpcRequest::ptr rrp = TrRpc::MessageFactory::create<TrRpc::RpcRequest>();
    // 所有请求对象的统一操作
    rrp->setId("111");
    rrp->setMtype(TrRpc::MType::REQ_RPC);
    // 进入特殊业务处理，转换成对应指针
    // dynamic_pointer_cast 将基类指针转换成子类(确保安全)
    rrp->setMethod("Add");
    Json::Value pramas;
    pramas["num1"] = 11;
    pramas["num2"] = 22;
    rrp->setParams(pramas);
    std::string msg = rrp->serialize();

    std::cout << "Rpc请求序列化后的结果: \n" << msg << std::endl;
    
    // ..中间无网络传输, 我们自己模拟收到了以后
    // 我们通过读取 msg 的前4个字节知道是什么MType
    TrRpc::BaseMessage::ptr frp = TrRpc::MessageFactory::create(TrRpc::MType::REQ_RPC);
    frp->deserialize(msg);
    auto rrp2 = std::dynamic_pointer_cast<TrRpc::RpcRequest>(frp); // 默认强转成功
    std::cout <<"check: " << rrp2->check() << std::endl;
    Json::Value pramas2;
    pramas2 = rrp2->params();
    std::cout << pramas2["num1"] << pramas2["num2"] << std::endl;
    // 假如处理完了
    auto req = TrRpc::MessageFactory::create<TrRpc::RpcResponse>();
    req->setRcode(TrRpc::RCode::RCODE_OK);
    Json::Value result;
    result["ans"] = 11 + 22;
    req->setResult(result);
    std::cout << "RpcResponse 序列化后的结果: \n" << req->serialize() << std::endl;

    std::cout << "-----------------------------------------" << std::endl;
    // 测试 TopicRequest 和 TopicResponse
    auto trp = TrRpc::MessageFactory::create<TrRpc::TopicRequest>();
    trp->setOptype(TrRpc::TopicOptype::TOPIC_CREATE);
    trp->setTopicKey("news");
    std::cout << "主题建立请求, 序列化后的结果: " << trp->serialize() << std::endl;
    // 这里简单一点，假设收到的就是 trp 了
    std::cout << trp->check() << std::endl;
    TrRpc::BaseMessage::ptr ftrq = TrRpc::MessageFactory::create(TrRpc::MType::RSP_TOPIC);
    auto trq = std::dynamic_pointer_cast<TrRpc::TopicResponse>(ftrq); // 默认强转成功
    trq->check();
    std::cout << "-----------------------------------------" << std::endl;

    // 测试 ServiceRequest 和 ServiceRespnse
    auto svrr = TrRpc::MessageFactory::create<TrRpc::ServiceRequest>();
    svrr->setMethod("Add");
    svrr->setHost({"127.0.0.1", 8085});
    svrr->setOptype(TrRpc::ServiceOptype::SERVICE_REGISTRY);
    svrr->check();
    std::cout << "注册请求: " << svrr->serialize() << std::endl;
    // 响应
    auto fsvrq = TrRpc::MessageFactory::create(TrRpc::MType::RSP_SERVICE);
    auto svrq = std::dynamic_pointer_cast<TrRpc::ServiceResponse>(fsvrq); // 默认强转成功
    std::vector<TrRpc::Address> addrs;
    addrs.push_back({"128.0.0.1", 8080});
    addrs.push_back({"128.0.1.2", 8081});
    addrs.push_back({"128.3.1.2", 8082});
    svrq->setHost(addrs);
    svrq->setOptype(TrRpc::ServiceOptype::SERVICE_DISCOVERY);
    svrq->setMethod("Sub");
    svrq->setRcode(TrRpc::RCode::RCODE_OK);
    svrq->check();
    std::cout << "发现响应: " << svrq->serialize() << std::endl;
    return 0;
}