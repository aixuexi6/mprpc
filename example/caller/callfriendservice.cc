#include <iostream>
#include "mprpcapplication.h"
#include "friend.pb.h"
#include "logger.h"
#include <unistd.h>
#include "nginxconfigupdater.h"

int main(int argc, char **argv)
{
    // 整个程序启动以后，想使用mprpc框架来享受rpc服务调用，需要先调用框架的初始化函数
    // MprpcApplication::Init(argc, argv);
    // 解析参数获取配置文件路径
    std::string config_file = "rpc.conf"; // 默认配置文件
    int opt;
    while ((opt = getopt(argc, argv, "i:")) != -1) {
        if (opt == 'i') {
            config_file = optarg;
        } else {
            std::cerr << "Usage: " << argv[0] << " -i <config_file>" << std::endl;
            return 1;
        }
    }
    
    // 检查是否提供了配置文件
    if (config_file.empty()) {
        std::cerr << "Config file must be specified with -i option" << std::endl;
        return 1;
    }
    
    // 使用重命名的方法初始化框架
    MprpcApplication::InitFromConfig(config_file);

    fixbug::FriendServiceRpc_Stub stub(new MprpcChannel());

    fixbug::GetFriendListRequest req;
    req.set_userid(6);
    fixbug::GetFriendListResponse res;

    // 发起rpc方法调用，RpcChannel::CallMethod统一做rpc方法调用数据的序列化和网络发送,
    // 最终会调用MprpcChannel::CallMethod,同步阻塞调用
    MprpcController controller;
    stub.GetFriendList(&controller, &req, &res, nullptr);

    // Login一次调用完成，读调用的结果
    if (controller.Failed())
    {
        std::cout << controller.ErrorText() << std::endl;
    }
    else
    {
        if (res.result().errcode() == 0)
        {
            std::cout << "rpc GetFriend response success:" << std::endl;
            int size = res.friends_size();
            for (int i = 0; i < size; i++)
            {
                std::cout << "index" << (i + 1) << "friend name:" << res.friends(i) << std::endl;
            }
        }
        else
        {
            std::cout << "rpc GetFriend response error:" << res.result().errmsg() << std::endl;
        }
    }
}