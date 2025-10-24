#pragma once
#include <string>
#include <unordered_map>

// 对各字段进行宏定义，方便后续的维护
namespace TrRpc
{                                 // 命名空间同名的话，会被合并
#define KEY_METHOD "method"       // Rpc的服务方法
#define KEY_PARAMS "parameters"   // 服务方法的参数
#define KEY_TOPIC_KEY "topic_key" // 主题名称
#define KEY_TOPIC_MSG "topic_msg" // 主题消息
#define KEY_OPTYPE "optype"       // 操作类型
#define KEY_HOST "host"           // 服务注册请求时: 一个封装 主机 ip 和 port 的 Json对象, 如果是 服务发现的响应时：可能是多个主机的 ip 和 port
#define KEY_HOST_IP "ip"
#define KEY_HOST_PORT "port"
#define KEY_RCODE "rcode"          // 服务完后的返回状态码
#define KEY_RESULT "result"        // 服务完后的结果

// 下面都是 Request 和 Response 中 针对上面不同核心业务数据的各种 "值"
// 如: method 数据的类型就是 string (用string 来描述一个方法, 因为到时候直接用函数名对应)
// 而：optype 可以是针对: 主题发布和订阅业务的各种操作(TopicOptype)，也可以是针对: 服务注册与发现业务中的操作(ServiceOptype)
// 于是我们就自定义了两个不同的类型，然后有不同的值来区分两个不同板块之间的操作
    enum class MType
    {
        REQ_RPC = 0,
        RSP_RPC,
        REQ_TOPIC,
        RSP_TOPIC,
        REQ_SERVICE,
        RSP_SERVICE
    };

    enum class RCode
    {
        RCODE_OK = 0,
        RCODE_PARSE_FAILED,
        RCODE_ERROR_MSGTYPE,
        RCODE_INVALID_MSG,
        RCODE_DISCONNECTED,
        RCODE_INVALID_PARAMS,
        RCODE_NOT_FOUND_SERVICE,
        RCODE_INVALID_OPTYPE,
        RCODE_NOT_FOUND_TOPIC,
        RCODE_INTERNAL_ERROR
    };
    static std::string errReason(RCode code)
    {
        static std::unordered_map<RCode, std::string> err_map = {
            {RCode::RCODE_OK, "成功处理！"},
            {RCode::RCODE_PARSE_FAILED, "消息解析失败！"},
            {RCode::RCODE_ERROR_MSGTYPE, "消息类型错误！"},
            {RCode::RCODE_INVALID_MSG, "无效消息"},
            {RCode::RCODE_DISCONNECTED, "连接已断开！"},
            {RCode::RCODE_INVALID_PARAMS, "无效的Rpc参数！"},
            {RCode::RCODE_NOT_FOUND_SERVICE, "没有找到对应的服务！"},
            {RCode::RCODE_INVALID_OPTYPE, "无效的操作类型"},
            {RCode::RCODE_NOT_FOUND_TOPIC, "没有找到对应的主题！"},
            {RCode::RCODE_INTERNAL_ERROR, "内部错误！"}};
        auto it = err_map.find(code);
        if (it == err_map.end())
        {
            return "未知错误！";
        }
        return it->second;
    }

    enum class RType
    {
        REQ_ASYNC = 0, // 异步请求
        REQ_CALLBACK   // 回调请求: 设置回调函数，通过回调函数对响应进行处理
    };

    enum class TopicOptype
    {
        TOPIC_CREATE = 0,
        TOPIC_REMOVE,
        TOPIC_SUBSCRIBE,
        TOPIC_CANCEL,
        TOPIC_PUBLISH   // 客户端给主题发布消息: 客户端到客户端不再是点对点的通信, 而是通过主题来连接的
    };

    enum class ServiceOptype
    {
        SERVICE_REGISTRY = 0,
        SERVICE_DISCOVERY,
        SERVICE_ONLINE,
        SERVICE_OFFLINE,
        SERVICE_UNKNOW
    };
}