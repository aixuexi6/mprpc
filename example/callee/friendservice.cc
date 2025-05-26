#include <iostream>
#include <string>
#include <vector>
#include "friend.pb.h"
#include "mprpcapplication.h"
#include "rpcprovider.h"

class FriendService : public fixbug::FriendServiceRpc
{
public:
    std::vector<std::string> GetFriendList(uint32_t userid)
    {
        std::cout << "doing GetFriendList service!userid:" <<userid<< std::endl;
        std::vector<std::string> vec;
        vec.push_back("rrr");
        vec.push_back("ppp");
        vec.push_back("vvv");
        return vec;
    }

    void GetFriendList(::google::protobuf::RpcController* controller,
                       const ::fixbug::GetFriendListRequest* request,
                       ::fixbug::GetFriendListResponse* response,
                       ::google::protobuf::Closure* done)
    {
        uint32_t id=request->userid();

        std::vector<std::string> vec=GetFriendList(id);

        fixbug::ResultCode *code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        for(std::string &name:vec)
        {
            std::string* p=response->add_friends();
            *p=name;
        }
        done->Run();
    }
};


int main(int argc,char **argv)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    //调用框架的初始化操作
    MprpcApplication::Init(argc,argv);

    //provider是一个rpc网络服务对象，把FriendService对象发布到rpc节点上
    RpcProvider provider;
    provider.NotifyService(new FriendService());

    //启动一个rpc服务发布节点 Run以后，进程进入阻塞状态，等待远程的rpc调用请求
    provider.Run();

    google::protobuf::ShutdownProtobufLibrary();

    return 0;
}