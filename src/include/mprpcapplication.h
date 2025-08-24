#pragma once

#include "mprpcconfig.h"

//mprpc框架的基础类
class MprpcApplication
{
public:
    // 保留原始初始化方法
    static void Init(int argc, char **argv, bool allowOverride = false);
    
    // 修改这个方法：移除默认参数，避免歧义
    static void InitWithOverrides(const std::string& config_file, 
                     const std::string& override_ip, 
                     const std::string& override_port, 
                     bool allowOverride = true);
    
    // 修改这个方法：重命名以避免歧义
    static void InitFromConfig(const std::string& config_file);

    static MprpcApplication& GetInstance();
    static MprpcConfig& GetConfig();
private:
    static MprpcConfig m_config;

    MprpcApplication(){}
    MprpcApplication(const MprpcApplication&)=delete;
    MprpcApplication(MprpcApplication&&)=delete;
};

