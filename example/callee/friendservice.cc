#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <getopt.h>
#include "friend.pb.h"
#include "mprpcapplication.h"
#include "rpcprovider.h"
#include "nginxconfigupdater.h"

class FriendService : public fixbug::FriendServiceRpc
{
public:
    // 添加端口参数
    explicit FriendService(int port) : port_(port) {}
    
    // 业务方法
    std::vector<std::string> GetFriendList(uint32_t userid)
    {
        LogRequest("GetFriendList");
        std::cout << "处理好友列表请求,用户ID: " << userid << std::endl;
        
        std::vector<std::string> vec;
        vec.push_back("好友A");
        vec.push_back("好友B");
        vec.push_back("好友C");
        return vec;
    }

    // RPC 方法实现
    void GetFriendList(::google::protobuf::RpcController* controller,
                       const ::fixbug::GetFriendListRequest* request,
                       ::fixbug::GetFriendListResponse* response,
                       ::google::protobuf::Closure* done) override
    {
        // 记录请求
        LogRequest("GetFriendList RPC");
        
        uint32_t id = request->userid();
        std::vector<std::string> vec = GetFriendList(id);

        fixbug::ResultCode *code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        for (std::string &name : vec) {
            std::string* p = response->add_friends();
            *p = name;
        }
        done->Run();
    }

private:
    int port_;  // 服务端口
    
    // 日志记录方法
    void LogRequest(const std::string& method_name) const {
        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::cout << "[" << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S")
                  << "." << std::setfill('0') << std::setw(3) << now_ms.count() << "] "
                  << "端口: " << port_ << " | 方法: " << method_name << std::endl;
    }
};

int main(int argc, char **argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    // 参数解析
    uint16_t port = 0;
    bool is_leader = false;
    std::string config_file;
    
    int opt;
    while ((opt = getopt(argc, argv, "p:i:l")) != -1) {
        switch (opt) {
            case 'p': port = static_cast<uint16_t>(std::stoi(optarg)); break;
            case 'i': config_file = optarg; break;
            case 'l': is_leader = true; break;
            default:
                std::cerr << "Usage: " << argv[0] 
                          << " -i <config_file> [-p <port>] [-l]" << std::endl;
                return 1;
        }
    }
    
    if (config_file.empty()) {
        std::cerr << "Error: Config file is required" << std::endl;
        return 1;
    }
    
    // 初始化框架 - 先加载配置文件
    MprpcApplication::InitFromConfig(config_file);
    
    // 获取配置对象
    MprpcConfig& config = MprpcApplication::GetConfig();
    
    // 如果命令行未指定端口，则从配置文件中获取
    if (port == 0) {
        std::string port_str = config.Load("rpcserverport");
        if (!port_str.empty()) {
            try {
                port = static_cast<uint16_t>(std::stoi(port_str));
            } catch (...) {
                std::cerr << "Error: Invalid port in config file: " << port_str << std::endl;
                return 1;
            }
        }
    }
    
    if (port == 0) {
        std::cerr << "Error: Port is required (specify with -p or in config)" << std::endl;
        return 1;
    }
    
    // 创建服务实例
    FriendService* service = new FriendService(port);
    RpcProvider provider;
    provider.NotifyService(service);
    
    // 创建共享的ZkClient实例
    std::string zk_ip = config.Load("zookeeperip");
    std::string zk_port = config.Load("zookeeperport");
    
    if (zk_ip.empty() || zk_port.empty()) {
        std::cerr << "Error: Zookeeper configuration missing" << std::endl;
        return 1;
    }
    
    std::string zk_host = zk_ip + ":" + zk_port;
    static ZkClient zk_client(zk_host);
    
    // Leader节点启动配置更新器
    if (is_leader) {
        std::string nginx_conf = config.Load("nginxconf");
        if (nginx_conf.empty()) {
            std::cerr << "Warning: nginxconf not specified in config" << std::endl;
        } else {
            static NginxConfigUpdater updater(zk_client, nginx_conf, "/FriendServiceRpc/GetFriendList");
            updater.Start();
            std::cout << "Started Nginx Config Updater (Leader Node)" << std::endl;
        }
    }

    // 启动服务
    std::cout << "Starting RPC Service on port: " << port << std::endl;
    provider.Run();
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}