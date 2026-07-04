include(FetchContent)

# EnTT を GitHub から自動取得 (Example 側でのみ使用)
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.16.0 # 必要に応じて適切なバージョンを指定
)
# 物理演算
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.5.0 # 必要に応じて適切なバージョンを指定
    SOURCE_SUBDIR  Build
)
set(TARGET_UNIT_TESTS OFF CACHE INTERNAL "")
set(TARGET_HELLO_WORLD OFF CACHE INTERNAL "")
set(TARGET_PERFORMANCE_TEST OFF CACHE INTERNAL "")
set(TARGET_SAMPLES OFF CACHE INTERNAL "")

if(MSVC)
  set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE INTERNAL "Use static MSVC runtime library")
endif()

FetchContent_MakeAvailable(entt JoltPhysics)

# ECSを用いたサンプルのビルド定義
add_executable(demo_cube_multiplayer
    cube_multiplayer/main.cpp # または EnTT に書き換えた新しいサンプルソース
)

target_link_libraries(demo_cube_multiplayer PRIVATE
    astream_core
    astream_util
    ${LIBS} ZLIB::ZLIB glm::glm
    EnTT::EnTT
    Jolt
)

astream_compile_assets(demo_cube_multiplayer
    "cube_multiplayer/Assets/cube.obj"
    # "cube_multiplayer/Assets/enemy.obj" (増やしたければ並べる)
)

# レンダリングのみ
add_executable(render_only renderer_sample.cpp)
target_link_libraries(render_only PRIVATE astream_core ${LIBS} glm::glm)

# チャットアプリサンプル
add_executable(demo_chat chat/main.cpp)
target_link_libraries(demo_chat PRIVATE astream_core astream_util ${LIBS} ZLIB::ZLIB)

