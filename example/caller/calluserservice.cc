#include <iostream>
#include "mprpcapplication.h"
#include "user.pb.h"
#include "mprpcchannel.h"

int main(int argc,char** argv)
{
    //整个程序启动以后，想使用mprpc框架来享受rpc服务调用，需要先调用框架的初始化函数
    MprpcApplication::Init(argc,argv);

    fixbug::UserServiceRpc_Stub stub(new MprpcChannel());

    fixbug::LoginRequest request;
    request.set_name("zhang san");
    request.set_pwd("123456");
    fixbug::LoginResponse response;

    fixbug::RegisterRequest req;
    req.set_id(2000);
    req.set_name("lmp");
    req.set_pwd("666666");
    fixbug::RegisterResponse res;

    //发起rpc方法调用，RpcChannel::CallMethod统一做rpc方法调用数据的序列化和网络发送,
    //最终会调用MprpcChannel::CallMethod,同步阻塞调用
    stub.Login(nullptr,&request,&response,nullptr);
    stub.Register(nullptr,&req,&res,nullptr);

    //Login一次调用完成，读调用的结果
    if(response.result().errcode()==0)
    {
        std::cout<<"rpc login response success:"<<response.success()<<std::endl;
    }
    else
    {
        std::cout<<"rpc login response error:"<<response.result().errmsg()<<std::endl;
    } 

    //Register一次调用完成，读调用的结果
    if(res.result().errcode()==0)
    {
        std::cout<<"rpc Register response success:"<<res.success()<<std::endl;
    }
    else
    {
        std::cout<<"rpc Register response error:"<<res.result().errmsg()<<std::endl;
    } 
    
    return 0;
}