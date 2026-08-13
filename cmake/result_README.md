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
- `LUASF_STUB_FILE`: absolute path to `stub/LuaSF.d.lua`.
- `LUASF_CALLBACK_CODECS_FILE`: absolute path to the schema-v1 `callback_codecs.json` manifest.
- `LUASF_RUNTIME_FILES`: all files that should be next to the executable at runtime.
- `LUASF_RUNTIME_DLLS`: compatibility alias for `LUASF_RUNTIME_FILES`.
- `luasf_copy_runtime_files(target)`: post-build copy helper for LuaSF, Lua, SFML, and Windows bundled MSVC redistributable runtime libraries when present.
- `luasf_copy_runtime_dlls(target)`: compatibility alias for Windows-oriented callers.

`LuaSF.d.lua` is a global declaration file starting with `---@meta`, so EmmyLua can expose its `sf` API from a dedicated stub library directory.

`callback_codecs.json` records the semantic callback conversion policies used by the generated bindings. Consumers should use `LUASF_CALLBACK_CODECS_FILE` instead of inferring special protocols from expanded `std::function` signatures. The adjacent `sfml_api.json` is the matching API snapshot for strict canonical-type validation.

`sf.SoundSource.EffectProcessor` uses flat 1-based borrowed buffer views and mutable frame-count refs. Its input view is read-only, its output view is write-only, input is `nil` at end of stream, and all borrowed values expire when the callback returns. Audio-thread lock contention bypasses the current block; Lua or contract faults latch the shared processor and are retrieved once on a logic thread through `LuaSF_take_deferred_callback_error`.
