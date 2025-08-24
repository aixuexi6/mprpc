#include "rpcprovider.h"
#include "mprpcapplication.h"
#include "rpcheader.pb.h"
#include "logger.h"
#include "zookeeperutil.h"

void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo service_info;
    //获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc=service->GetDescriptor();
    //获取服务名字
    std::string service_name=pserviceDesc->name();
    //获取服务对象service的方法数量
    int methodCnt=pserviceDesc->method_count();

    LOG_INFO("service name:%s",service_name.c_str());

    for(int i=0;i<methodCnt;i++)
    {
        //获得服务对象指定下标服务方法的描述（抽象描述）
        const google::protobuf::MethodDescriptor* _pmethodDesc=pserviceDesc->method(i);
        std::string method_name=_pmethodDesc->name();
        service_info.m_methodMap.insert({method_name,_pmethodDesc});
        LOG_INFO("method name:%s",method_name.c_str());
    }
    service_info.m_service=service;
    m_serviceMap.insert({service_name,service_info});
}

void RpcProvider::Run() {
    std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
    muduo::net::InetAddress address(ip, port);

    // 创建TcpServer对象
    muduo::net::TcpServer server(&m_eventLoop, address, "RpcProvider");
    server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1,
                        std::placeholders::_2, std::placeholders::_3));
    server.setThreadNum(4);

    // 获取ZooKeeper配置
    std::string zk_ip = MprpcApplication::GetInstance().GetConfig().Load("zookeeperip");
    std::string zk_port = MprpcApplication::GetInstance().GetConfig().Load("zookeeperport");
    
    if (zk_ip.empty() || zk_port.empty()) {
        LOG_ERR("ZooKeeper configuration missing!");
        return;
    }
    std::string zk_host = zk_ip + ":" + zk_port;

    // 使用正确的ZooKeeper地址
    ZkClient zkCli(zk_host);
    zkCli.Start();

    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!zkCli.IsConnected()) {
        LOG_ERR("Failed to connect to ZooKeeper at %s", zk_host.c_str());
        return;
    }

    // 集群模式注册服务
    for (auto &sp : m_serviceMap) {
        // 1. 创建服务根节点（持久节点）
        std::string service_root = "/" + sp.first;
        if (!zkCli.Exists(service_root.c_str())) {
            zkCli.Create(service_root.c_str(), nullptr, 0, 0); // 0表示持久节点
            LOG_INFO("Created service root: %s", service_root.c_str());
        }
        
        // 2. 为每个方法创建实例节点
        for (auto &mp : sp.second.m_methodMap) {
            // 方法节点路径
            std::string method_path = service_root + "/" + mp.first;
            
            // 如果方法节点不存在，则创建
            if (!zkCli.Exists(method_path.c_str())) {
                zkCli.Create(method_path.c_str(), nullptr, 0, 0); // 持久节点
                LOG_INFO("Created method node: %s", method_path.c_str());
            }
            
            // 3. 在方法节点下创建服务实例节点（临时顺序节点）
            std::string instance_path = method_path + "/instance-";
            char instance_data[128];
            snprintf(instance_data, sizeof(instance_data), "%s:%d", ip.c_str(), port);
            
            // 创建临时顺序节点
            zkCli.Create(instance_path.c_str(), instance_data, strlen(instance_data), ZOO_EPHEMERAL | ZOO_SEQUENCE);
            LOG_INFO("Registered service instance: %s at %s", instance_path.c_str(), instance_data);
        }
    }

    // 启动服务
    LOG_INFO("RPC Provider starting at %s:%d", ip.c_str(), port);
    server.start();
    m_eventLoop.loop();
}

void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
{
    if(!conn->connected())
    {
        //和rpc client的连接断开了
        conn->shutdown();
    }
}

void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn, 
                            muduo::net::Buffer *buffer, 
                            muduo::Timestamp)
{
    //网络上接受的远程rpc请求的字符流 Login args
    std::string recv_buf=buffer->retrieveAllAsString();

    //从字符流读取前4个字节的内容
    uint32_t header_size=0;
    uint32_t net_header_size = 0;
    recv_buf.copy(reinterpret_cast<char*>(&net_header_size), 4, 0);

    // 将网络字节序转换为主机字节序
    header_size = ntohl(net_header_size);
    
    //根据header_size读取数据头的原始字符流，反序列化数据，得到rpc请求的详细信息
    std::string rpc_header_str=recv_buf.substr(4,header_size);

    mprpc::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;
    uint32_t args_size;
    if(rpcHeader.ParseFromString(rpc_header_str))
    {
        //数据头反序列化成功
        service_name=rpcHeader.service_name();
        method_name=rpcHeader.method_name();
        args_size=rpcHeader.arg_size();
    }
    else
    {
        //数据头反序列化失败
        std::cout<<"rpc_header_str:"<<rpc_header_str<<"parse error!"<<std::endl;
        return;
    }

    //获取rpc方法参数的字符流数据
    std::string args_str=recv_buf.substr(4+header_size,args_size);

    //打印调试信息
    std::cout<<"============================="<<std::endl;
    std::cout<<"head_size:"<<header_size<<std::endl;
    std::cout<<"rpc_header_str:"<<rpc_header_str<<std::endl;
    std::cout<<"service_name:"<<service_name<<std::endl;
    std::cout<<"method_name:"<<method_name<<std::endl;
    std::cout<<"args_size:"<<args_size<<std::endl;
    std::cout<<"args_str:"<<args_str<<std::endl;
    std::cout<<"============================="<<std::endl;

    //获取service对象和method对象
    auto it=m_serviceMap.find(service_name);
    if(it==m_serviceMap.end()){
        std::cout<<service_name<<"is not exist!"<<std::endl;
        return;
    }

    auto mit=it->second.m_methodMap.find(method_name);
    if(mit==it->second.m_methodMap.end()){
        std::cout<<service_name<<":"<<method_name<<"is not exist!"<<std::endl;
        return;
    }

    google::protobuf::Service *service=it->second.m_service;//获取service对象 new UserService
    const google::protobuf::MethodDescriptor *method=mit->second;//获取method对象 Login

    //生成rpc方法调用的请求request和响应response参数
    google::protobuf::Message *request=service->GetRequestPrototype(method).New();
    if(!request->ParseFromString(args_str))
    {
        std::cout<<"request parse error,content:"<<args_str<<std::endl;
        return;
    }
    google::protobuf::Message *response=service->GetResponsePrototype(method).New();

    //给下面的method方法的调用，绑定一个Closure的回调函数
    google::protobuf::Closure *done=google::protobuf::NewCallback<RpcProvider,
                            const muduo::net::TcpConnectionPtr &, google::protobuf::Message*>
                            (this,&RpcProvider::SendRpcResponce,conn,response);     

    //在框架上根据远端rpc请求，调用rpc节点上的发布的方法
    //new UserService().Login(method,nullptr,request,response)
    service->CallMethod(method,nullptr,request,response,done);
}

void RpcProvider::SendRpcResponce(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message* responce)
{
    std::string responce_str;
    if(responce->SerializeToString(&responce_str))
    {
        //序列化成功，通过网络将rpc执行的结果返回调用方
        conn->send(responce_str);
    }
    else
    {
        std::cout<<"Serialize responce error!"<<std::endl;
    }
    conn->shutdown();
}