#include "nginxconfigupdater.h"
#include "logger.h"
#include "mprpcapplication.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <algorithm>
#include <thread>


NginxConfigUpdater::~NginxConfigUpdater() {
    Stop();
}

void NginxConfigUpdater::Start() {
    if (running_) {
        LOG_INFO("Config updater already running");
        return;
    }
    
    LOG_INFO("Starting Nginx config updater thread");
    running_ = true;
    watch_thread_ = std::thread([this]() {
        LOG_INFO("Config updater thread started, ID: %lu", 
                std::hash<std::thread::id>{}(std::this_thread::get_id()));
        
        // 首次立即更新
        UpdateConfig("initial update");
        
        // 防抖计时器
        auto last_update = std::chrono::steady_clock::now();
        
        while (running_) {
            LOG_INFO("Registering Watcher for service path: %s", watch_path_.c_str());
            
            // 注册Watcher并等待事件
            try {
                zk_client_.WatchChildren(watch_path_.c_str(), 
                    [this, &last_update](int rc, const std::vector<std::string>& children) {
                        if (rc == ZOK) {
                            // 防抖处理：5秒内只允许一次更新
                            auto now = std::chrono::steady_clock::now();
                            if (now - last_update < std::chrono::seconds(5)) {
                                LOG_INFO("Watcher event throttled (within 5s cooldown)");
                                return;
                            }
                            last_update = now;
                            
                            LOG_INFO("Watcher triggered: service nodes changed");
                            std::lock_guard<std::mutex> lock(mtx_);
                            config_updated_ = true;
                            cv_.notify_one();
                        } else {
                            LOG_ERR("Watcher error: %s", zerror(rc));
                        }
                    });
                
                LOG_INFO("Waiting for Watcher event or timeout (30s)...");
                
                // 等待事件或超时
                std::unique_lock<std::mutex> lock(mtx_);
                if (cv_.wait_for(lock, std::chrono::seconds(30), [this]() {
                    return config_updated_ || !running_;
                })) {
                    if (config_updated_) {
                        config_updated_ = false;
                        UpdateConfig("watcher event");
                    }
                } else {
                    // 30秒超时，主动更新配置
                    LOG_INFO("Watcher timeout, updating configuration");
                    UpdateConfig("timeout");
                }
            } catch (const std::exception& e) {
                LOG_ERR("Error in watch thread: %s", e.what());
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    });
    
    // 分离线程
    watch_thread_.detach();
}

void NginxConfigUpdater::Stop() {
    if (!running_) return;
    
    LOG_INFO("Stopping Nginx config updater");
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        cv_.notify_one();
    }
    
    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }
}

void NginxConfigUpdater::UpdateConfig(const std::string& trigger) {
    LOG_INFO("Updating Nginx configuration (trigger: %s)", trigger.c_str());
    
    try {
        // 获取所有节点
        LOG_INFO("Fetching service nodes from Zookeeper: %s", watch_path_.c_str());
        std::vector<std::string> nodes = zk_client_.GetChildren(watch_path_.c_str());
        
        LOG_INFO("Found %ld service nodes", nodes.size());
        
        // 收集所有提供者地址
        std::vector<std::string> providers;
        for (const auto& node : nodes) {
            std::string full_path = watch_path_ + "/" + node;
            std::string node_data = zk_client_.GetData(full_path.c_str());
            if (!node_data.empty()) {
                // 验证节点数据格式
                if (node_data.find(':') != std::string::npos) {
                    LOG_INFO("Found valid provider: %s", node_data.c_str());
                    providers.push_back(node_data);
                } else {
                    LOG_ERR("Invalid provider format: %s", node_data.c_str());
                }
            } else {
                LOG_ERR("Empty data for node: %s", full_path.c_str());
            }
        }
        
        // 检查服务列表是否变化
        if (providers == last_providers_) {
            LOG_INFO("Service list unchanged, skipping update");
            return;
        }
        
        LOG_INFO("Service list changed, updating configuration");
        last_providers_ = providers;
        
        // 生成并更新配置
        GenerateNginxConfig(providers);
        ReloadNginx();
        
    } catch (const std::exception& e) {
        LOG_ERR("Error updating Nginx config: %s", e.what());
    }
}

void NginxConfigUpdater::GenerateNginxConfig(const std::vector<std::string>& providers) {
    LOG_INFO("Generating Nginx config at: %s", nginx_conf_path_.c_str());
    
    std::ofstream conf_file(nginx_conf_path_);
    if (!conf_file) {
        LOG_ERR("Failed to open Nginx config: %s", nginx_conf_path_.c_str());
        return;
    }
    
    // 只生成 upstream 配置块
    conf_file << "# 动态生成的 RPC 后端配置 - 请勿手动编辑\n";
    conf_file << "upstream rpc_backend {\n"
              << "    least_conn;\n"
              << "    zone rpc_zone 64k;\n";
    
    // 添加提供者服务器
    if (providers.empty()) {
        LOG_ERR("No active providers, using placeholder configuration");
        conf_file << "    # 等待服务注册...\n";
        conf_file << "    server 127.0.0.1:9999 down; # 占位符\n";
    } else {
        for (const auto& provider : providers) {
            conf_file << "    server " << provider << ";\n";
        }
    }
    
    conf_file << "}\n";
    
    LOG_INFO("Generated Nginx upstream config with %ld providers", providers.size());
}

void NginxConfigUpdater::ReloadNginx() {
    LOG_INFO("Reloading Nginx configuration...");
    
    // 获取全局配置对象
    MprpcConfig& config = MprpcApplication::GetConfig();
    
    // 检查配置语法
    LOG_INFO("Testing Nginx configuration syntax");
    std::string nginx_main_conf = config.Load("nginx_main_conf");
    if (nginx_main_conf.empty()) {
        LOG_ERR("'nginx_main_conf' not found in configuration");
        return;
    }
    
    // 使用主配置文件测试语法
    std::string test_cmd = "sudo nginx -t -c " + nginx_main_conf;
    LOG_INFO("Executing: %s", test_cmd.c_str());
    
    if (system(test_cmd.c_str()) != 0) {
        LOG_ERR("Nginx configuration test failed");
        return;
    }
    
    // 重载配置
    LOG_INFO("Sending reload signal to Nginx");
    if (system("sudo /usr/sbin/nginx -s reload") != 0) {
        LOG_ERR("Failed to reload Nginx");
    } else {
        LOG_INFO("Nginx reloaded successfully");
    }
}