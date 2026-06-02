# LuaSF AutoGenerator

LuaSF AutoGenerator generates a CMake-based Lua module that exposes SFML to Lua through sol2. The generated module can be consumed either from source or from a collected binary package.

The generated Lua module is named `LuaSF`. In CMake, consumers link against `LuaSF::LuaSF`.

## Requirements

- CMake 3.21 or newer for the consumer example below.
- A C++20-capable compiler when building LuaSF from source.
- Python 3 with `venv` support for generation scripts.

The bundled dependency versions are recorded in `versions.conf`.

## Build LuaSF

Initialise dependencies once:

```bat
init.bat
```

```sh
sh init.sh
```

Generate and build the LuaSF CMake project:

```bat
build.bat Release
```

```sh
sh build.sh Release
```

This creates the generated source project under `output/` and builds the LuaSF dynamic library and Lua language-server stub.

To collect a redistributable package:

```bat
collect_result.bat Release
```

```sh
sh collect_result.sh Release
```

The collected package is written to `output/result/`.

## Use From A CMake Project

There are two supported integration styles:

- **Packaged integration**: copy `output/result/` into your project, for example as `LuaSF/`.
- **Source integration**: copy or vendor the generated `output/` source project into your project, for example as `LuaSF/`.

The public target is the same in both cases: `LuaSF::LuaSF`.

### Packaged Integration

Use this when you have already run `collect_result.bat` or `collect_result.sh` and copied `output/result/` into your project.

```cmake
cmake_minimum_required(VERSION 3.21)

project(SFLua LANGUAGES C CXX)

add_subdirectory(LuaSF)

add_executable(SFLua main.cpp)

target_link_libraries(SFLua PRIVATE
    LuaSF::LuaSF
)

luasf_copy_runtime_dlls(SFLua)
```

`luasf_copy_runtime_dlls(SFLua)` copies all bundled LuaSF, Lua, SFML, and platform runtime libraries from the package into the executable output directory. `luasf_copy_runtime_files(SFLua)` is also available and is the platform-neutral name.

### Source Integration

Use this when you have copied or vendored the generated `output/` source project into your project.

```cmake
cmake_minimum_required(VERSION 3.21)

project(SFLua LANGUAGES C CXX)

set(LUASF_LUA_STUB_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/LuaSF.lua")

add_subdirectory(LuaSF)

add_executable(SFLua main.cpp)

target_compile_features(SFLua PRIVATE cxx_std_17)

target_include_directories(SFLua PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/LuaSF/third_party/Lua"
)

target_link_libraries(SFLua PRIVATE
    LuaSF::LuaSF
    LuaSF_lua_shared
)

luasf_copy_runtime_dlls(SFLua)
```

`LUASF_LUA_STUB_OUTPUT`, the Lua include directory, and `LuaSF_lua_shared` are only needed when your own executable needs the generated Lua stub path or calls the bundled Lua C API directly. If your executable only links to LuaSF and never includes Lua headers, you can omit those lines. `luasf_copy_runtime_dlls(SFLua)` is available in both source and packaged integration.

## Lua Example

After the executable can load the LuaSF module and runtime libraries, Lua code can create and draw SFML objects directly. This example opens a window, enables 8x anti-aliasing, draws a green circle, and exits when the window is closed.

```lua
local sf = require("LuaSF")

-- Create a window
local mode = sf.VideoMode.new(sf.Vector2u.new(800, 600))
local ctx = sf.ContextSettings.new()
ctx.antiAliasingLevel = 8
local window = sf.RenderWindow.new(mode, "LuaSF Test", sf.Style.Titlebar | sf.Style.Close, sf.State.Windowed, ctx)

-- Set the background color
local bg = sf.Color.new(50, 50, 50)

-- Create a green circle
local circle = sf.CircleShape.new(80)
circle:setFillColor(sf.Color.new(0, 220, 80))
circle:setPosition(sf.Vector2f.new(320, 220))

-- Main loop
while window:isOpen() do
    -- Process all pending events
    while true do
        local event = window:pollEvent()
        if event == nil then
            break
        end
        if event:isClosed() then
            window:close()
            break
        end
    end

    window:clear(bg)
    window:draw(circle)
    window:display()
end
```

## Packaged CMake Items

The collected package exposes these CMake items:

- `LuaSF::LuaSF`: imported LuaSF dynamic-library target.
- `LuaSF::Lua`: imported bundled Lua dynamic-library target.
- `LUASF_STUB_FILE`: absolute path to `stub/LuaSF.lua`.
- `LUASF_RUNTIME_FILES`: bundled runtime files that should be placed next to the executable.
- `LUASF_RUNTIME_DLLS`: compatibility alias for `LUASF_RUNTIME_FILES`.
- `luasf_copy_runtime_files(target)`: post-build copy helper for all bundled runtime files.
- `luasf_copy_runtime_dlls(target)`: compatibility alias for `luasf_copy_runtime_files(target)`.

## Runtime Notes

LuaSF is built as a dynamic library. At runtime, the executable must be able to load LuaSF itself, the Lua runtime, and the SFML runtime libraries. Use `luasf_copy_runtime_dlls(target)` or `luasf_copy_runtime_files(target)` to copy the required runtime libraries next to your executable.
