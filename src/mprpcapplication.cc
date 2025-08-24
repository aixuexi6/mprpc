#include "mprpcapplication.h"
#include "iostream"
#include <unistd.h>
#include <string>

MprpcConfig MprpcApplication::m_config;

void ShowArgHelp() {
    std::cout << "Usage: program -i <configfile> [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -i <file>   Specify configuration file (required)" << std::endl;
    std::cout << "  -I <ip>     Override rpcserverip in config" << std::endl;
    std::cout << "  -P <port>   Override rpcserverport in config" << std::endl;
}

// 实现重命名的方法
void MprpcApplication::InitWithOverrides(const std::string& config_file, 
                     const std::string& override_ip, 
                     const std::string& override_port, 
                     bool allowOverride) {
    // 加载基础配置文件
    m_config.LoadConfigFile(config_file.c_str());
    
    // 允许命令行参数覆盖配置文件中的值
    if (allowOverride) {
        if (!override_ip.empty()) {
            m_config.SetConfig("rpcserverip", override_ip);
        }
        if (!override_port.empty()) {
            m_config.SetConfig("rpcserverport", override_port);
        }
    }

    // // 调试输出
    // std::cout << "Final config values:" << std::endl;
    // std::cout << "  rpcserverip: " << m_config.Load("rpcserverip") << std::endl;
    // std::cout << "  rpcserverport: " << m_config.Load("rpcserverport") << std::endl;
    // std::cout << "  zookeeperip: " << m_config.Load("zookeeperip") << std::endl;
    // std::cout << "  zookeeperport: " << m_config.Load("zookeeperport") << std::endl;
}

void MprpcApplication::InitFromConfig(const std::string& config_file) {
    // 实现内容
    m_config.LoadConfigFile(config_file.c_str());
    
    // 调试输出
    std::cout << "Config values from " << config_file << ":\n"
              << "  rpcserverip: " << m_config.Load("rpcserverip") << "\n"
              << "  rpcserverport: " << m_config.Load("rpcserverport") << "\n"
              << "  zookeeperip: " << m_config.Load("zookeeperip") << "\n"
              << "  zookeeperport: " << m_config.Load("zookeeperport") << "\n"
              << "  nginxconf: " << m_config.Load("nginxconf") << std::endl;
}

void MprpcApplication::Init(int argc, char **argv, bool allowOverride) {
    if (argc < 2) {
        ShowArgHelp();
        exit(EXIT_FAILURE);
    }

    std::string config_file;
    std::string override_ip;
    std::string override_port;
    
    int c = 0;
    // 扩展参数解析：支持 -i(配置文件)、-I(覆盖IP)、-P(覆盖端口)
    while ((c = getopt(argc, argv, "i:I:P:")) != -1) {
        switch (c) {
        case 'i': // 配置文件路径
            config_file = optarg;
            break;
        case 'I': // 覆盖IP地址
            override_ip = optarg;
            break;
        case 'P': // 覆盖端口号
            override_port = optarg;
            break;
        case '?':
        case ':':
            ShowArgHelp();
            exit(EXIT_FAILURE);
        default:
            break;
        }
    }

    // 检查配置文件是否指定
    if (config_file.empty()) {
        std::cerr << "Config file must be specified with -i option" << std::endl;
        ShowArgHelp();
        exit(EXIT_FAILURE);
    }

    // 调用重命名的方法
    InitWithOverrides(config_file, override_ip, override_port, allowOverride);
}

MprpcApplication &MprpcApplication::GetInstance()
{
    static MprpcApplication app;
    return app;
}

MprpcConfig& MprpcApplication::GetConfig()
{
    return m_config;
}