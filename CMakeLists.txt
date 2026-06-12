cmake_minimum_required(VERSION 3.12)
project(AsciiStreamEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(ZLIB  REQUIRED)

# 数学ライブラリのリンクが必要な場合があるため
if(UNIX)
  set(LIBS m)
endif()

add_subdirectory(core)

# サーバー側
add_executable(server server.cpp)
target_link_libraries(server PRIVATE ascii3d_core ${LIBS} ZLIB::ZLIB)

# クライアント側
add_executable(client client.cpp)
target_link_libraries(client PRIVATE ascii3d_core ${LIBS} ZLIB::ZLIB)

# テスト
add_executable(test renderer_sample.cpp)
target_link_libraries(test PRIVATE ascii3d_core ${LIBS})
