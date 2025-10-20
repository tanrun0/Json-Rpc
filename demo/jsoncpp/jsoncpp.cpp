#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include "jsoncpp/json/json.h"

// jsoncpp 序列化和反序列化的要点：
// 1. 都要通过 Jason::Value 对象中转
// 2. 都要利用工厂（父类）来创建(new)子类对象，注意：使用完要释放
// 3. 序列化接口: write (会把序列化得到的字符串输出到流中), parse(把字符串反序列化回 Json::Value)

// 将传入的 Json::Value 对象序列化，得到的字符串输入 str
bool Serialize(const Json::Value &val, std::string *str)
{
    std::stringstream ss;
    Json::StreamWriterBuilder swb;
    // 禁用 Unicode 转义，让中文直接显示（老的解释器会把中文进行 Unicode 转义，这里不兼容它们）
    // swb.settings_["escapeUnicode"] = false; // 但是好像没用
    std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter());
    int ret = sw->write(val, &ss);
    if (ss.fail()) // 用流的状态检查是否出错
    {
        std::cout << "Serialize Failed" << std::endl;
        return false;
    }
    *str = ss.str();
    return true;
}

bool DeSerialize(const std::string &str, Json::Value *val)
{
    Json::String errs;
    Json::CharReaderBuilder crb;
    std::unique_ptr<Json::CharReader> cr(crb.newCharReader());
    int ret = cr->parse(str.c_str(), str.c_str() + str.size(), val, &errs);
    if (ret == false)
    {
        std::cout << "Deserialize Failed: " << errs << std::endl;
        return false;
    }
    return true;
}

int main()
{
    Json::Value stu;
    stu["姓名"] = "zhangsan";
    stu["年龄"] = 20;
    stu["成绩"].append(75); // 会自动创建一个数组作为 "成绩" 的 value
    stu["成绩"].append(85);
    stu["成绩"].append(95);
    // json可以嵌套
    Json::Value hobbies;
    hobbies["书籍"] = "《活着》";
    hobbies["运动"] = "跑步";
    stu["爱好"] = hobbies;

    // 测试
    std::string stu_str;
    Serialize(stu, &stu_str);
    std::cout << "序列化后: " << stu_str << std::endl;
    std::cout << "------------------------------------" << std::endl;
    Json::Value de_stu;
    DeSerialize(stu_str, &de_stu);
    std::cout << "反序列化后: " << std::endl;
    std::cout << "姓名: " << de_stu["姓名"] << std::endl;
    std::cout << "年龄: " << de_stu["年龄"] << std::endl;
    std::cout << "成绩: ";
    for (auto s : de_stu["成绩"])
        std::cout << s << ',';
    std::cout << std::endl;
    std::cout << "书籍: " << de_stu["爱好"]["书籍"] << std::endl;
    std::cout << "运动: " << de_stu["爱好"]["运动"] << std::endl;

    return 0;
}