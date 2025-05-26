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

void RpcProvider::Run() 
{
    std::string ip=MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port=atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
    muduo::net::InetAddress address(ip,port);

    //创建TcpServer对象
    muduo::net::TcpServer server(&m_eventLoop,address,"RpcProvider");
    //绑定连接回调和消息读写回调 分离了网络代码和业务代码
    server.setConnectionCallback(std::bind(&RpcProvider::OnConnection,this,std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::OnMessage,this,std::placeholders::_1,
                        std::placeholders::_2,std::placeholders::_3));
    //设置muduo库线程数量
    server.setThreadNum(4);

    //把当前rpc节点要发布的服务全部注册到zk上面，使得rpc client可以从zk上发现服务
    ZkClient zkCli;
    zkCli.Start();

    for(auto &sp:m_serviceMap)
    {
        std::string service_path="/"+sp.first;
        zkCli.Create(service_path.c_str(),nullptr,0);
        for(auto &mp:sp.second.m_methodMap)
        {
            std::string method_path=service_path+"/"+mp.first;
            char method_path_data[128]={0};
            sprintf(method_path_data,"%s:%d",ip.c_str(),port);
            //ZOO_EPHEMERAL表示znode是临时性节点
            zkCli.Create(method_path.c_str(),method_path_data,strlen(method_path_data),ZOO_EPHEMERAL);
        }
    }

    std::cout<<"rpcprovider start service at ip:"<<ip<<" port:"<<port<<std::endl;

    //启动网络服务
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

/*
在框架内部，RpcProvider和RpcConsumer协商好通信用的protobuf数据类型
service_name method_name args           定义proto的message类型，进行数据头的序列化和反序列化
16UserServiceLogin zhangsan123456       service_name method_name args_size
header_size(4字节) + header_str + args_str
*/
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn, 
                            muduo::net::Buffer *buffer, 
                            muduo::Timestamp)
{
    //网络上接受的远程rpc请求的字符流 Login args
    std::string recv_buf=buffer->retrieveAllAsString();
    //std::cout << "recv_buf: " << recv_buf << std::endl;

    //从字符流读取前4个字节的内容
    uint32_t header_size=0;
    recv_buf.copy((char*)&header_size,4,0);

    //std::cout << "Header size: " << header_size << std::endl;
    
    //根据header_size读取数据头的原始字符流，反序列化数据，得到rpc请求的详细信息
    std::string rpc_header_str=recv_buf.substr(4,header_size);

    //std::cout << "Rpc header string: " << rpc_header_str << std::endl;

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