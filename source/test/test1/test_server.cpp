#include "../../common/net.hpp"
#include "../../common/dispatcher.hpp"
#include "../../server/rpc_server.hpp"

void Add(const Json::Value &params, Json::Value &result)
{
    DBG_LOG("成功进入 Add 函数");
    int sum = params["num1"].asInt() + params["num2"].asInt();
    result = sum; // result 是一个对象
}

int main()
{
    auto sd_factory = std::make_shared<TrRpc::server::SDescribeFactory>();
    sd_factory->setMethodName("Add");
    sd_factory->setParamsDesc("num1", TrRpc::server::VType::INTEGRAL);
    sd_factory->setParamsDesc("num2", TrRpc::server::VType::INTEGRAL);
    sd_factory->setReturnType(TrRpc::server::VType::INTEGRAL);
    sd_factory->setCallback(Add);
    TrRpc::server::RpcServer server(TrRpc::Address("127.0.0.1", 9090));
    server.registerMethod(sd_factory->build());
    server.start();
    std::cout << "服务器启动，监听端口 8085" << std::endl;
    return 0;
}