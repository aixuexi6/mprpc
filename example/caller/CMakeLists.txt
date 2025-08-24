#set(SRC_LIST calluserservice.cc ../user.pb.cc)
set(SRC_LIST callfriendservice.cc ../friend.pb.cc)

add_executable(consumer ${SRC_LIST})
target_link_libraries(consumer mprpc protobuf)

# file(COPY ${PROJECT_SOURCE_DIR}/bin/mprpc.ini 
#      DESTINATION ${EXECUTABLE_OUTPUT_PATH})