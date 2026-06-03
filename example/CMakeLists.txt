cmake_minimum_required(VERSION 3.21)

project(SFLua LANGUAGES C CXX)

# Copy output/result/ into example/LuaSF/ before building:
#   cp -r ../output/result ./LuaSF

add_subdirectory(LuaSF)

add_executable(SFLua main.cpp)

target_compile_definitions(SFLua PRIVATE
    SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Scripts"
)

target_link_libraries(SFLua PRIVATE
    LuaSF::LuaSF
)

luasf_copy_runtime_dlls(SFLua)
