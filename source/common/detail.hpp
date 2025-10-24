#pragma once
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include "jsoncpp/json/json.h"
#include <iostream>
#include <chrono>
#include <random>
#include <atomic>
#include <iomanip>

// 宏不受命名空间影响
namespace TrRpc
{
// 设置等级，实现对不同打印信息的控制
#define INF 0
#define DBG 1
#define ERR 2

#define LOGLEVEL DBG
    // 日志宏
// 宏定义的 '\'最好是行尾最后一个字符，和后面的换行符之间最后不要有空格
#define LOG(level, format, ...)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if (level < LOGLEVEL)                                                                                          \
            break;                                                                                                     \
        time_t t = time(NULL);                                                                                         \
        struct tm *ltm = localtime(&t);                                                                                \
        char tmp[32] = {0};                                                                                            \
        strftime(tmp, 31, "%H:%M:%S", ltm);                                                                            \
        fprintf(stdout, "[%p %s %s:%d] " format "\n", (void *)pthread_self(), tmp, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
// 在 C 中, 连续的字符串，如: "hello "  "world"  -> 会合并成一个 "hello world"
// __VA__ARGS : 使用可变参数,   ## 的作用是拼接，当用 ##__VA__ARGS时有特殊用法: 当 __VA__ARGS为空的时候，删除前面的',' 避免语法错误[用宏可变参数带上这个就行了]
#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)

    class JsonUtil
    {
    public:
        // 将传入的 Json::Value 对象序列化，得到的字符串输入 str
        static bool Serialize(const Json::Value &val, std::string *str)
        {
            std::stringstream ss;
            Json::StreamWriterBuilder swb;
            // 禁用 Unicode 转义，让中文直接显示（老的解释器会把中文进行 Unicode 转义，这里不兼容它们）
            // swb.settings_["escapeUnicode"] = false; // 但是好像没用
            std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter());
            int ret = sw->write(val, &ss);
            if (ss.fail()) // 用流的状态检查是否出错
            {
                ERR_LOG("Serialize Failed");
                return false;
            }
            *str = ss.str();
            return true;
        }

        static bool DeSerialize(const std::string &str, Json::Value *val)
        {
            Json::String errs;
            Json::CharReaderBuilder crb;
            std::unique_ptr<Json::CharReader> cr(crb.newCharReader());
            int ret = cr->parse(str.c_str(), str.c_str() + str.size(), val, &errs);
            if (ret == false)
            {
                // 日志宏使用C实现的，所以这里要传 errs.c_str()
                ERR_LOG("Deserialize Failed: %s", errs.c_str());
                return false;
            }
            return true;
        }
    };

    // 生成 Uid（唯一编码，由 32 位 16 进制数字字符组成）
    // 前 16 位（前8个字节）为随机数，后 16 位（后8个字节）为自增序号，进行双重保障
    // 1 个字节有 8 个比特位: 4 个比特位可以表示一个 16 进制数
    // 格式: 550e8400-e29b-41d4-a716-446655440000 (四个'-'的分割成: 8, 4, 4, 12)
    class UUid
    {
    public:
        static std::string uuid()
        {
            // 如果用系统时间作为种子，在短时间内，种子一样。
            // 所以可以用机器随机数(慢), 所以我们用机器随机数(确保唯一)作为种子伪随机数(带来高效)的种子
            std::stringstream uid; // 存储最后的 uid
            // 1. 构造机器随机数对象
            std::random_device rd;
            // 2. 用机器随机数为种子构造伪随机数对象
            std::mt19937 generator(rd());
            // 3. 限制随机数范围: 0 - 255: 因为一个字节是 8 个比特位，刚好生成一个字节的随机数
            std::uniform_int_distribution<int> distribution(0, 255);
            // 4. 生成 8 个字节的随机数，按指定格式组织
            for(int i = 0; i < 8; i++)
            {
                if(i == 4 || i == 6) uid << "-";
                // std::hex << : 其实就是把一个字节上的内容转换成 2 个16进制数
                // setw 默认右对齐，如果要设置左对齐要用 std::left
                uid << std::setw(2) << std::setfill('0') << std::hex << distribution(generator); 
            }
            uid << "-";
            // 下面是自增序号，一个 8 字节的序号 (size_t 是一个 8 字节无符号的整数)
            static std::atomic<size_t> seq(1); // 设置成全局的
            ssize_t cur = seq.fetch_add(1); // 自增 1，返回的是自增前的数
            for(int i = 7; i >= 0; i--) 
            {
                if(i == 5) uid << "-";
                // (cur >> (i * 8)) & 0xFF : 提取单个字节(每次右移 8 个bit位其实就是右移了 1 个字节)
                uid << std::setw(2) << std::setfill('0') << std::hex << ((cur >> (i * 8)) & 0xFF);
            }
            return uid.str();
        }
    };

}
