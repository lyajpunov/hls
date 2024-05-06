# 设置CMake最低版本要求  
cmake_minimum_required(VERSION 3.10)  
  
# 设置项目名称  
project(ServerProject)  
  
# 添加子目录（如果有的话）  
# add_subdirectory(subdirectory_name)  
  
# 设置可执行文件的输出目录为bin  
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  
  
# 设置源文件列表  
add_executable(server ./server/server.cpp)  
add_executable(client ./client/client.cpp)  

  
# 如果需要链接库，可以使用target_link_libraries  
# 例如，target_link_libraries(server some_library)  
 