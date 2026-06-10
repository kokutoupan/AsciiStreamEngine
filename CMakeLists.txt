cmake_minimum_required(VERSION 3.10)
project(Ascii3DStream)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(ZLIB  REQUIRED)

# 数学ライブラリのリンクが必要な場合があるため
if(UNIX)
  set(LIBS m)
endif()

# # サーバー側
# add_executable(server server.cpp)
# target_link_libraries(server ${LIBS} ZLIB::ZLIB)
#
# # クライアント側
# add_executable(client client.c)
# target_link_libraries(client ${LIBS} ZLIB::ZLIB)

# テスト
add_executable(test renderer_sample.cpp)
target_link_libraries(test ${LIBS})
