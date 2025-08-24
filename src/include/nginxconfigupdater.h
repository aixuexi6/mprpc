#ifndef NGINXCONFIGUPDATER_H
#define NGINXCONFIGUPDATER_H

#include "zookeeperutil.h"
#include "mprpcapplication.h"  // 添加此头文件
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>

class NginxConfigUpdater {
public:
    NginxConfigUpdater(ZkClient& zk_client, 
                  const std::string& nginx_conf_path,
                  const std::string& watch_path)  // 新增参数
    : zk_client_(zk_client), 
      nginx_conf_path_(nginx_conf_path),
      watch_path_(watch_path) {}  // 初始化
      
    ~NginxConfigUpdater();
    
    void Start();
    void Stop();

private:
    void UpdateConfig(const std::string& trigger);
    void GenerateNginxConfig(const std::vector<std::string>& providers);
    void ReloadNginx();
    
    ZkClient& zk_client_;
    std::string nginx_conf_path_;
    std::thread watch_thread_;
    std::atomic<bool> running_{false};
    std::mutex mtx_;
    std::condition_variable cv_;
    bool config_updated_{false};
    std::string watch_path_ = "/FriendServiceRpc/GetFriendList";
    
    // 添加的成员变量：存储上一次的服务提供者列表
    std::vector<std::string> last_providers_;
};

#endif // NGINXCONFIGUPDATER_H