#include "../../server/rpc_server.hpp"

int main()
{
    TrRpc::server::RegistryServer server(8080); // 这是注册中心
    server.Start();
    return 0;
}