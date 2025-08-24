#pragma once

#define ZOO_USE_LEGACY_SYNC_API 1
#include <zookeeper/zookeeper.h>

#include <semaphore.h>
#include <string>
#include <vector>
#include "logger.h"

#include <semaphore.h>
#include <zookeeper/zookeeper.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>

class ZkClient {
public:
    ZkClient();
    explicit ZkClient(const std::string& host);
    ~ZkClient();
    
    void Start();
    void Create(const char *path, const char *data, int datalen, int state = 0);
    std::string GetData(const char *path);
    std::vector<std::string> GetChildren(const char* path);
    
    // 节点存在检查
    bool Exists(const char* path);
    
    // 异步操作上下文
    struct AsyncContext {
        sem_t sem;
        std::string result;
        std::vector<std::string> children;
        int rc = ZOK;
    };
    
    // 异步回调类型
    using DataCallback = std::function<void(int rc, const std::string& data)>;
    using ChildrenCallback = std::function<void(int rc, const std::vector<std::string>& children)>;

    // 异步API
    void GetDataAsync(const char* path, DataCallback callback);
    void GetChildrenAsync(const char* path, ChildrenCallback callback);
    
    // 添加Watcher API
    void WatchChildren(const char* path, ChildrenCallback callback);
    
    bool IsConnected() const {
        return m_zhandle != nullptr && zoo_state(m_zhandle) == ZOO_CONNECTED_STATE;
    }

private:
    zhandle_t* m_zhandle;
    sem_t m_sem;
    
    // 异步回调函数
    static void dataCompletion(int rc, const char* value, int value_len, 
                              const struct Stat* stat, const void* data);
    static void childrenCompletion(int rc, const String_vector* strings, 
                                  const void* data);
    
    // Watcher回调函数
    static void watcherCallback(zhandle_t* zh, int type, int state, 
                               const char* path, void* watcherCtx);
};