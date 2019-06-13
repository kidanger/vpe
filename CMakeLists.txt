cmake_minimum_required(VERSION 3.14)
project(vpe)

add_subdirectory(tiny-process-library)

set(CMAKE_CXX_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_executable(vpe
    main.cpp
    imgui-1.70/examples/imgui_impl_opengl3.cpp
    imgui-1.70/examples/imgui_impl_opengl3.h
    imgui-1.70/examples/imgui_impl_sdl.cpp
    imgui-1.70/examples/imgui_impl_sdl.h
    imgui-1.70/misc/cpp/imgui_stdlib.cpp
    imgui-1.70/imconfig.h
    imgui-1.70/imgui.cpp
    imgui-1.70/imgui.h
    imgui-1.70/imgui_draw.cpp
    imgui-1.70/imgui_widgets.cpp
    imgui-1.70/imgui_internal.h
    imgui-1.70/imstb_rectpack.h
    imgui-1.70/imstb_textedit.h
    imgui-1.70/imstb_truetype.h
    imgui-1.70/examples/libs/gl3w/GL/gl3w.c
    imgui-1.70/examples/libs/gl3w/GL/gl3w.h
    imgui-1.70/examples/libs/gl3w/GL/glcorearb.h
    ImNodes/ImNodes.h
    ImNodes/ImNodes.cpp
    nodes.cpp
)
target_include_directories(vpe PUBLIC
    ${SDL2_INCLUDE_DIRS}
    imgui-1.70
    imgui-1.70/examples
    imgui-1.70/examples/libs/gl3w
    ImNodes
)

target_link_libraries(vpe PUBLIC ${SDL2_LIBRARIES} -ldl -lGL)
target_link_libraries(vpe PUBLIC tiny-process-library)
target_compile_options(vpe PUBLIC ${SDL2_CFLAGS_OTHER})
target_compile_definitions(vpe PUBLIC -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)

target_compile_options(vpe PUBLIC -g)


