# LuaSF Result Package

Copy this folder into another CMake project, then add it as a subdirectory.

```cmake
add_subdirectory(path/to/LuaSF-result)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE LuaSF::LuaSF)
luasf_copy_runtime_files(my_app)
```

Use `luasf_copy_runtime_files(my_app)` when you want all bundled runtime
libraries copied. CMake only sees runtime files from imported targets, while
LuaSF also needs the bundled SFML runtime libraries in `bin`. On Windows,
`bin` also includes the bundled MSVC redistributable DLLs.

Available CMake items:

- `LuaSF::LuaSF`: imported LuaSF dynamic-library target, with include directories attached.
- `LuaSF::Lua`: imported Lua dynamic-library target.
- `LUASF_STUB_FILE`: absolute path to `stub/LuaSF.lua`.
- `LUASF_RUNTIME_FILES`: all files that should be next to the executable at runtime.
- `LUASF_RUNTIME_DLLS`: compatibility alias for `LUASF_RUNTIME_FILES`.
- `luasf_copy_runtime_files(target)`: post-build copy helper for LuaSF, Lua, SFML, and Windows bundled MSVC redistributable runtime libraries when present.
- `luasf_copy_runtime_dlls(target)`: compatibility alias for Windows-oriented callers.
