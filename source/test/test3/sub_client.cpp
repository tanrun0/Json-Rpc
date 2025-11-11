#include "../../client/rpc_client.hpp"
#include <thread>

void callback(const std::string &key, const std::string &msg)
{
    INF_LOG("收到了 %s 主题的 %s 消息", key.c_str(), msg.c_str());
}

int main()
{
    auto client = std::make_shared<TrRpc::client::TopicClient>("127.0.0.1", 8085);
    // 不管是 subscribe 还是 publish 客户端都要先创建，避免不存在
    bool ret = client->create("sport");
    if (ret == false)
    {
        ERR_LOG("创建主题失败");
    }
    client->subscribe("sport", callback);
    // 等待->退出
    std::this_thread::sleep_for(std::chrono::seconds(10));
    client->shutdown();
    return 0;
}