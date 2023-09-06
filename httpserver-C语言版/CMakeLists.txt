# CMake最低版本要求
cmake_minimum_required(VERSION 3.0)

# project name  
project(Reactor_server)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall -g -o2 -lpthread")

# 指定当前目录下的源文件，保存到<ALL_SRCS>变量中
aux_source_directory(${CMAKE_SOURCE_DIR}/src ALL_SRCS)

#PROJECT_SOURCE_DIR:当前工程目录下
set(EXECUTABLE_OUTPUT_PATH 
${PROJECT_SOURCE_DIR}
) 

# 头文件路径
include_directories(${CMAKE_SOURCE_DIR}/include) 

# 可执行文件生成
add_executable(${PROJECT_NAME} main.c ${ALL_SRCS})

# 链接linux自带的多线程共享库:pthread  --->相同于指定g++编译器-l参数
target_link_libraries(${PROJECT_NAME} pthread)


