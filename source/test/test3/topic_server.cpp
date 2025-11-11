#include "../../server/rpc_server.hpp"

int main()
{
    auto server = std::make_shared<TrRpc::server::TopicServer>(8085);
    server->Start();
    return 0;
}