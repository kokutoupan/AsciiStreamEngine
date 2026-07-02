project(astream_util LANGUAGES CXX)


if(NOT SOURCE_FILES)
  add_library(astream_util INTERFACE)

  target_include_directories(astream_util INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
  )

  # core ライブラリターゲットに依存させる (core の include パスなどを引き継ぐ)
  target_link_libraries(astream_util INTERFACE astream_core)
else()
  add_library(astream_util
    # ソースファイルが増えたらここに追加
    # src/TextureUtil.cpp などを置く場合
  )

  # ソースファイルがある（ビルドが必要な）通常パターンの場合
  target_include_directories(astream_util PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
  target_link_libraries(astream_util PUBLIC astream_core)
endif()
