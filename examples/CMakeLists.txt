add_executable(demo_cube_multiplayer cube_multiplayer/main.cpp)
target_link_libraries(demo_cube_multiplayer PRIVATE astream_core ${LIBS} ZLIB::ZLIB glm::glm)
astream_compile_assets(demo_cube_multiplayer
    "cube_multiplayer/Assets/cube.obj"
    # "cube_multiplayer/Assets/enemy.obj" (増やしたければ並べる)
)

# レンダリングのみ
add_executable(render_only renderer_sample.cpp)
target_link_libraries(render_only PRIVATE astream_core ${LIBS} glm::glm)

