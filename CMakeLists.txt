#设置cmake的最低版本和项目名称
cmake_minimum_required(VERSION 3.0)
project(mprpc)

set(CMAKE_BUILD_TYPE "Debug")

#设置项目可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
#设置项目库文件输出路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

#设置项目编译头文件搜索路径
# include_directories(${PROJECT_SOURCE_DIR}/src/include)
# include_directories(${PROJECT_SOURCE_DIR}/example)
include_directories(
    ${PROJECT_SOURCE_DIR}/src/include
    ${PROJECT_SOURCE_DIR}/example
    /usr/local/include
)
#设置项目库文件搜索路径
# link_directories(${PROJECT_SOURCE_DIR}/lib)
link_directories(
    ${PROJECT_SOURCE_DIR}/lib
    /usr/local/lib
)

#包含框架所有代码
add_subdirectory(src)
#包含框架使用的实例代码
add_subdirectory(example)

# file(COPY ${PROJECT_SOURCE_DIR}/bin/test.conf 
#      DESTINATION ${EXECUTABLE_OUTPUT_PATH}
#      RENAME mprpc.ini)

