# LuaSF AutoGenerator

English | [简体中文](README_zh_CN.md)

LuaSF AutoGenerator generates a CMake-based Lua module that exposes SFML to Lua through sol2. The generated module can be consumed either from source or from a collected binary package.

The generated Lua module is named `LuaSF`. In CMake, consumers link against `LuaSF::LuaSF`.

## Requirements

- CMake 3.21 or newer for the consumer example below.
- A C++20-capable compiler when building LuaSF from source.
- Python 3.12 or newer for generation scripts.

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

This creates the generated source project under `output/` and builds the embedded LuaSF dynamic library, the plain Lua extension module, and the Lua language-server stub.

To collect a redistributable package:

```bat
collect_result.bat Release
```

```sh
sh collect_result.sh Release
```

The collected packages are written to `output/result/embedded/` and `output/result/extension/`.

To pack redistributable zip archives after collect:

```bat
pack_result.bat
```

```sh
sh pack_result.sh
```

Archives are written to `output/packages/`:

- `LuaSF-source.{tar.gz|zip}` — generated source project from `output/`, including `callback_codecs.json`, without `bin/` or `build/`
- `LuaSF-embedded-{OS}-{ARCH}-{COMPILER}.{tar.gz|zip}` — embedded package, including `callback_codecs.json` and its matching `sfml_api.json` validation snapshot
- `LuaSF-extension-{OS}-{ARCH}-{COMPILER}.{tar.gz|zip}` — Lua extension package

`pack_result.sh` creates `.tar.gz` archives. `pack_result.bat` creates `.zip` archives.

## Use From A CMake Project

There are three supported integration styles:

- **Packaged integration**: copy `output/result/embedded/` into your project, for example as `LuaSF/`.
- **Source integration**: copy or vendor the generated `output/` source project into your project, for example as `LuaSF/`.
- **Plain Lua extension**: copy or reference `output/result/extension/bin/` from Lua and run `require("LuaSF")`.

The CMake integration target for embedded use is `LuaSF::LuaSF`. The plain Lua extension is a separate build output.

### Plain Lua Extension

Use this when a normal Lua runtime should load LuaSF with `require("LuaSF")`.

The extension build is selected by the `LUASF_LUA_EXTENSION` macro. The generated `LuaSF_lua_extension` CMake target defines this macro and exports `luaopen_LuaSF`. The default `LuaSF` / `LuaSF::LuaSF` target does not define this macro, so source integration in an IDE remains the embedded version unless you explicitly build the extension target or define the macro yourself.

After collecting results, put `output/result/extension/bin/` on `package.cpath`:

```lua
package.cpath = [[path/to/extension/bin/?.dll;]] .. package.cpath
local sf = require("LuaSF")
```

The extension package does not bundle `lua.dll`; it is loaded by the host Lua runtime and must match the extension's Lua ABI.

### Packaged Integration

Use this when you have already run `collect_result.bat` or `collect_result.sh` and copied `output/result/embedded/` into your project.

```cmake
cmake_minimum_required(VERSION 3.21)

project(SFLua LANGUAGES C CXX)

add_subdirectory(LuaSF)

add_executable(SFLua main.cpp)

target_compile_definitions(SFLua PRIVATE
    SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Scripts"
)

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

set(LUASF_LUA_STUB_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/stub/LuaSF.d.lua")

add_subdirectory(LuaSF)

add_executable(SFLua main.cpp)

target_compile_features(SFLua PRIVATE cxx_std_17)

target_include_directories(SFLua PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/LuaSF/third_party/Lua/src"
)

target_compile_definitions(SFLua PRIVATE
    SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Scripts"
)

target_link_libraries(SFLua PRIVATE
    LuaSF::LuaSF
    LuaSF_lua_shared
)

luasf_copy_runtime_dlls(SFLua)
```

`LUASF_LUA_STUB_OUTPUT`, the Lua include directory, and `LuaSF_lua_shared` are only needed when your own executable needs the generated Lua stub path or calls the bundled Lua C API directly. If your executable only links to LuaSF and never includes Lua headers, you can omit those lines. `luasf_copy_runtime_dlls(SFLua)` is available in both source and packaged integration.

## C++ Example

After linking against `LuaSF::LuaSF`, this example creates a Lua state with all LuaSF bindings registered, then runs `Entry.lua` from the `Scripts/` folder. `SCRIPTS_DIR` is baked at build time via `target_compile_definitions` (shown in the CMake examples above), so the path works regardless of the current working directory.

```cpp
#include <LuaSF.hpp>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#ifndef SCRIPTS_DIR
#define SCRIPTS_DIR "."
#endif

int main()
{
    lua_State* L = LuaSF_create_state();
    if (L == nullptr)
        return 1;

    if (luaL_dofile(L, SCRIPTS_DIR "/Entry.lua") != LUA_OK)
    {
        fprintf(stderr, "Lua error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    lua_close(L);
    return 0;
}
```

When using source integration, the `target_include_directories` for the Lua headers (shown in the source CMake example above) must be active so that `<lua.h>` and `<lauxlib.h>` are found.

## Lua Example

After the executable can load the LuaSF module and runtime libraries, Lua code can create and draw SFML objects directly. This example opens a window, enables 8x anti-aliasing, draws a green circle, and exits when the window is closed.

```lua
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
- `LUASF_STUB_FILE`: absolute path to `stub/LuaSF.d.lua`.
- `LUASF_CALLBACK_CODECS_FILE`: absolute path to the schema-v1 `callback_codecs.json` manifest.
- `LUASF_RUNTIME_FILES`: bundled runtime files that should be placed next to the executable.
- `LUASF_RUNTIME_DLLS`: compatibility alias for `LUASF_RUNTIME_FILES`.
- `luasf_copy_runtime_files(target)`: post-build copy helper for all bundled runtime files.
- `luasf_copy_runtime_dlls(target)`: compatibility alias for `luasf_copy_runtime_files(target)`.

The generated `.d.lua` is a global declaration file and starts with `---@meta`, allowing EmmyLua to expose the `sf` API from a dedicated stub library directory.

`callback_codecs.json` describes callback conversions by semantic C++ alias or an exact function-parameter use site. Consumers should read this generated manifest instead of matching expanded `std::function` signatures, and may validate its canonical types against the adjacent `sfml_api.json` snapshot. In particular, `sf::SoundSource::EffectProcessor` is the only selector for the interleaved-float audio codec; an unrelated alias with the same canonical signature does not opt into that protocol.

### Sound effect callback contract

`sf.SoundSource.EffectProcessor` is exposed as a five-argument, no-return callback:

```lua
fun(
    inputFrames: sf.ReadOnlyFloatBufferView|nil,
    inputFrameCount: sf.UIntRef,
    outputFrames: sf.WriteOnlyFloatBufferView,
    outputFrameCount: sf.UIntRef,
    frameChannelCount: integer
)
```

Buffer indices are 1-based flat interleaved samples; `#view` and `view:size()` are sample counts. `UIntRef.value` is mutable, while `UIntRef.capacity` is read-only, and both are measured in frames. Input is `nil` at end of stream, allowing an effect to emit buffered tail audio. The callback return value is ignored.

Views and refs borrow SFML's native buffers and expire permanently when the callback returns. Input writes, output reads, invalid indices or counts, and expired access are errors. The audio thread tries to enter the Lua state without waiting: contention and state shutdown bypass the effect for the current block. A Lua or contract error latches the shared processor context, so its copies subsequently bypass; the fixed-capacity deferred error can be consumed once on a logic thread with `LuaSF_take_deferred_callback_error`.

## Runtime Notes

LuaSF is built as a dynamic library. At runtime, the executable must be able to load LuaSF itself, the Lua runtime, and the SFML runtime libraries. Use `luasf_copy_runtime_dlls(target)` or `luasf_copy_runtime_files(target)` to copy the required runtime libraries next to your executable.

## License

This project is licensed under the [MIT License](LICENSE).

## Third-Party Licenses

Bundled dependency versions are recorded in `versions.conf`. Their licenses are:

| Dependency | Version | License |
| --- | --- | --- |
| [SFML-ME](https://github.com/JasonLeon01/SFML-ME/tree/310ME) | `310ME` branch | [zlib/libpng](https://opensource.org/licenses/Zlib) — see `third_party/SFML/license.md` |
| [Lua](https://www.lua.org/) | 5.5.0 | [MIT](https://www.lua.org/license.html) |
| [sol2](https://github.com/ThePhD/sol2) | 3.3.0 | [MIT](https://github.com/ThePhD/sol2/blob/develop/LICENSE.txt) |

SFML may also redistribute external libraries under their own licenses; see the SFML documentation and `third_party/SFML/license.md` for details.
