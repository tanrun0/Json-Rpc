#include "../../client/rpc_client.hpp"

int main()
{
    auto client = std::make_shared<TrRpc::client::TopicClient>("127.0.0.1", 8085);
    // 不管是 subscribe 还是 publish 客户端都要先创建，避免不存在
    bool ret = client->create("sport");
    if(ret == false)
    {
        ERR_LOG("创建主题失败");
    }
    for(int i = 0; i < 3; i++)
    {
        std::string msg = "NBA" + std::to_string(i);
        client->publish("sport", msg);
    }
    client->shutdown();
    return 0;
}