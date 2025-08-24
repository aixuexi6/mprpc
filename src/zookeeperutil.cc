#include "zookeeperutil.h"
#include "mprpcapplication.h"
#include "logger.h"
#include <iostream>
#include <cstring>
#include <functional>
#include <memory>
#include <ctime>
#include <cerrno>
#include <utility>
#include <mutex>
#include <unordered_set>

// 全局watcher回调
void global_watcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx) {
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            sem_t *sem = (sem_t*)zoo_get_context(zh);
            if (sem) sem_post(sem);
        }
    }
}

ZkClient::ZkClient() : m_zhandle(nullptr) {
    sem_init(&m_sem, 0, 0);
}

ZkClient::ZkClient(const std::string& host) : m_zhandle(nullptr) {
    sem_init(&m_sem, 0, 0);
    m_zhandle = zookeeper_init(host.c_str(), global_watcher, 30000, nullptr, nullptr, 0);
    if (m_zhandle) {
        zoo_set_context(m_zhandle, &m_sem);
    }
}

ZkClient::~ZkClient() {
    if (m_zhandle) {
        zookeeper_close(m_zhandle);
    }
    sem_destroy(&m_sem);
}

void ZkClient::Start() {
    if (IsConnected()) return;
    
    // 设置超时时间 (5秒)
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        LOG_ERR("clock_gettime error: %s", strerror(errno));
        return;
    }
    ts.tv_sec += 5;  // 5秒超时
    
    // 等待连接信号
    int result = sem_timedwait(&m_sem, &ts);
    if (result == -1) {
        if (errno == ETIMEDOUT) {
            LOG_ERR("Connection to ZooKeeper timed out");
        } else {
            LOG_ERR("sem_timedwait error: %s", strerror(errno));
        }
    } else {
        LOG_INFO("Connected to ZooKeeper");
    }
}

void ZkClient::Create(const char *path, const char *data, int datalen, int state) {
    // 异步上下文
    struct Context {
        sem_t sem;
        int rc = ZOK;
    } context;
    sem_init(&context.sem, 0, 0);
    
    // 异步创建节点
    zoo_acreate(m_zhandle, path, data, datalen, 
               &ZOO_OPEN_ACL_UNSAFE, state,
               [](int rc, const char* value, const void* data) {
                   Context* ctx = (Context*)data;
                   ctx->rc = rc;
                   sem_post(&ctx->sem);
               }, 
               &context);
    
    // 等待异步完成
    sem_wait(&context.sem);
    
    if (context.rc != ZOK) {
        LOG_ERR("Failed to create znode %s: %s", path, zerror(context.rc));
    } else {
        LOG_INFO("Created znode %s", path);
    }
    
    sem_destroy(&context.sem);
}

bool ZkClient::Exists(const char* path) {
    struct Context {
        sem_t sem;
        bool exists = false;
    } context;
    sem_init(&context.sem, 0, 0);
    
    zoo_aexists(m_zhandle, path, 0, 
               [](int rc, const struct Stat *stat, const void *data) {
                   Context* ctx = (Context*)data;
                   ctx->exists = (rc == ZOK);
                   sem_post(&ctx->sem);
               }, 
               &context);
    
    sem_wait(&context.sem);
    sem_destroy(&context.sem);
    return context.exists;
}

std::string ZkClient::GetData(const char *path) {
    AsyncContext context;
    sem_init(&context.sem, 0, 0);
    
    // 异步获取数据
    zoo_aget(m_zhandle, path, 0, dataCompletion, &context);
    
    // 等待异步完成
    sem_wait(&context.sem);
    
    if (context.rc != ZOK) {
        LOG_ERR("Error getting data for %s: %s", path, zerror(context.rc));
    }
    
    sem_destroy(&context.sem);
    return context.result;
}

std::vector<std::string> ZkClient::GetChildren(const char* path) {
    AsyncContext context;
    sem_init(&context.sem, 0, 0);
    
    // 异步获取子节点
    zoo_aget_children(m_zhandle, path, 0, childrenCompletion, &context);
    
    // 等待异步完成
    sem_wait(&context.sem);
    
    if (context.rc != ZOK) {
        LOG_ERR("Error getting children for %s: %s", path, zerror(context.rc));
    }
    
    sem_destroy(&context.sem);
    return context.children;
}

// 异步API实现
void ZkClient::GetDataAsync(const char* path, DataCallback callback) {
    auto context = std::make_shared<AsyncContext>();
    sem_init(&context->sem, 0, 0);
    
    zoo_aget(m_zhandle, path, 0, 
        [](int rc, const char* value, int value_len, const struct Stat* stat, const void* data) {
            auto ctx = (AsyncContext*)data;
            ctx->rc = rc;
            if (rc == ZOK && value) {
                ctx->result = std::string(value, value_len);
            }
            sem_post(&ctx->sem);
        }, 
        context.get());
    
    // 启动线程等待并执行回调
    std::thread([context, callback]() {
        sem_wait(&context->sem);
        callback(context->rc, context->result);
        sem_destroy(&context->sem);
    }).detach();
}

void ZkClient::GetChildrenAsync(const char* path, ChildrenCallback callback) {
    auto context = std::make_shared<AsyncContext>();
    sem_init(&context->sem, 0, 0);
    
    zoo_aget_children(m_zhandle, path, 0, 
        [](int rc, const String_vector* strings, const void* data) {
            auto ctx = (AsyncContext*)data;
            ctx->rc = rc;
            if (rc == ZOK && strings) {
                for (int i = 0; i < strings->count; ++i) {
                    ctx->children.push_back(strings->data[i]);
                }
            }
            sem_post(&ctx->sem);
        }, 
        context.get());
    
    // 启动线程等待并执行回调
    std::thread([context, callback]() {
        sem_wait(&context->sem);
        callback(context->rc, context->children);
        sem_destroy(&context->sem);
    }).detach();
}

// 修改 Watcher 实现
void ZkClient::WatchChildren(const char* path, ChildrenCallback callback) {
    // 创建上下文对象
    auto watcherCtx = new std::pair<ChildrenCallback, ZkClient*>(callback, this);
    
    // 使用awget_children设置watcher
    int rc = zoo_awget_children(m_zhandle, path, 
        watcherCallback,  // 使用静态回调函数
        watcherCtx,       // 上下文
        [](int rc, const String_vector* strings, const void* data) {
            auto ctx = static_cast<std::pair<ChildrenCallback, ZkClient*>*>(const_cast<void*>(data));
            
            // 添加详细的错误处理
            if (rc != ZOK) {
                LOG_ERR("WatchChildren callback error: %s", zerror(rc));
                delete ctx;
                return;
            }
            
            std::vector<std::string> children;
            if (strings) {
                for (int i = 0; i < strings->count; ++i) {
                    children.push_back(strings->data[i]);
                }
            }
            
            // 记录接收到的子节点
            // LOG_INFO("Received %ld children for path: %s", children.size(), path);
            for (const auto& child : children) {
                LOG_INFO("  - %s", child.c_str());
            }
            
            ctx->first(rc, children);
        }, 
        watcherCtx);
    
    if (rc != ZOK) {
        LOG_ERR("Failed to set watcher for path %s: %s", path, zerror(rc));
        delete watcherCtx;
    } else {
        LOG_INFO("Watcher registered for path: %s", path);
    }
}

// 静态watcher回调函数
void ZkClient::watcherCallback(zhandle_t* zh, int type, int state, 
                              const char* path, void* watcherCtx) {
    if (type == ZOO_CHILD_EVENT) {
        LOG_INFO("Watcher event: child change on path %s", path);
        
        // 重新设置watcher
        auto ctx = static_cast<std::pair<ChildrenCallback, ZkClient*>*>(watcherCtx);
        ctx->second->WatchChildren(path, ctx->first);
    } else if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            LOG_INFO("Watcher: session reconnected");
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            LOG_ERR("Watcher: session expired");
        }
    } else {
        LOG_INFO("Watcher event: type=%d, state=%d, path=%s", type, state, path ? path : "null");
    }
}

// 静态回调函数实现
void ZkClient::dataCompletion(int rc, const char* value, int value_len, 
                             const struct Stat* stat, const void* data) {
    AsyncContext* context = (AsyncContext*)data;
    context->rc = rc;
    if (rc == ZOK && value) {
        context->result = std::string(value, value_len);
    }
    sem_post(&context->sem);
}

void ZkClient::childrenCompletion(int rc, const String_vector* strings, 
                                 const void* data) {
    AsyncContext* context = (AsyncContext*)data;
    context->rc = rc;
    if (rc == ZOK && strings) {
        for (int i = 0; i < strings->count; ++i) {
            context->children.push_back(strings->data[i]);
        }
    }
    sem_post(&context->sem);
}