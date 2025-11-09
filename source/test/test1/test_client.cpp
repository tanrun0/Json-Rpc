#include "../../common/net.hpp"
#include "../../common/dispatcher.hpp"
#include "../../client/rpc_client.hpp"
#include <thread>



// 对结果进行回调
void Add_callback(const Json::Value & result)
{
    DBG_LOG("callback result: %d", result.asInt());
}


int main()
{
    auto client = std::make_shared<TrRpc::client::RpcClient>(false, "127.0.0.1", 9090);

    Json::Value params, result;
    params["num1"] = 10;
    params["num2"] = 20;
    bool ret = client->call("Add", params, result);
    if(ret != false)
    {
        DBG_LOG("result: %d", result.asInt());
    }
    params["num1"] = 30;
    params["num2"] = 40;
    ret = client->call("Add", params, Add_callback);
    if(ret == false)
    {
        DBG_LOG("异步回调出错");
    }
    params["num1"] = 50;
    params["num2"] = 60;
    TrRpc::client::RpcCaller::JsonAsyncResponse res_future;
    ret = client->call("Add", params, res_future);
    if(ret != false)
    {
        DBG_LOG("异步获取result: %d", res_future.get().asInt());
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}