# LuaSF AutoGenerator

English | [简体中文](README_zh_CN.md)

LuaSF AutoGenerator generates a CMake-based Lua module that exposes SFML to Lua through sol2. The generated module can be consumed either from source or from a collected binary package.

The generated Lua module is named `LuaSF`. In CMake, consumers link against `LuaSF::LuaSF`.

LuaSF is a binder only. It does not bundle Lua, sol2 or SFML, and it does not create or close the `lua_State`: the host owns the VM and hands it to LuaSF through `LuaSF_register`.

## Requirements

- CMake 3.21 or newer for the consumer example below.
- A C++20-capable compiler when building LuaSF from source.
- Python 3.12 or newer for generation scripts.

The SFML variants and the bundled dependency versions are recorded in `versions.conf`.

## SFML Variants

LuaSF binds one SFML build at a time. Select the variant with the second positional argument of `init` and `build`:

| Argument | SFML source |
| --- | --- |
| *(omitted)* | upstream SFML `SFML_VERSION` |
| `ME` | `SFML_ME_REPOSITORY` at `SFML_ME_TAG` |
| `ME-OH` | `SFML_ME_OH_REPOSITORY` at `SFML_ME_OH_TAG` |

The variant only selects which SFML is downloaded and which bindings are generated. Platform differences (iOS, Android, OHOS) are decided by `CMAKE_SYSTEM_NAME`, so a single source tree covers every variant.

## Build LuaSF

Initialise dependencies once:

```bat
init.bat ME
```

```sh
sh init.sh ME
```

Generate and build the LuaSF CMake project:

```bat
build.bat Release ME
```

```sh
sh build.sh Release ME
```

This creates the generated source project under `output/` and builds the embedded LuaSF dynamic library, the host `luac`, and the Lua language-server stub.

Switching variants replaces `third_party/SFML`; `init` and `build` both verify that the checked-out SFML matches the requested variant before doing any work.

On macOS arm64 with AppleClang, Release builds use `-Os` for the binding units and module registration entry point, while retaining Release LTO. The callback codec, state lifecycle support, and dependencies keep their existing optimization settings. Other platforms, architectures, compilers, and configurations retain their existing optimization settings.

To collect a redistributable package:

```bat
collect_result.bat Release
```

```sh
sh collect_result.sh Release
```

The collected package is written to `output/result/embedded/`.

To pack redistributable archives after collect:

```bat
pack_result.bat
```

```sh
sh pack_result.sh
```

Archives are written to `output/packages/`:

| Variant | Source archive | Embedded archive |
| --- | --- | --- |
| upstream SFML | `LuaSF-source.{tar.gz\|zip}` | `LuaSF-embedded-{OS}-{ARCH}-{COMPILER}.{tar.gz\|zip}` |
| `ME` | `LuaSF-source-ME.{tar.gz\|zip}` | `LuaSF-embedded-ME-{OS}-{ARCH}-{COMPILER}.{tar.gz\|zip}` |
| `ME-OH` | `LuaSF-source-ME-OH.{tar.gz\|zip}` | `LuaSF-embedded-ME-OH-{OS}-{ARCH}-{COMPILER}.{tar.gz\|zip}` |

`pack_result.sh` creates `.tar.gz` archives. `pack_result.bat` creates `.zip` archives.

## Use From A CMake Project

There are two supported integration styles:

- **Packaged integration**: copy `output/result/embedded/` into your project, for example as `LuaSF/`.
- **Source integration**: copy or vendor the generated `output/` source project into your project, for example as `LuaSF/`.

The CMake integration target for embedded use is `LuaSF::LuaSF`.

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

The source project has no vendored dependencies. Point it at your own Lua, sol2 and SFML checkouts through the `LUASF_LUA_ROOT`, `LUASF_SOL2_ROOT` and `LUASF_SFML_ROOT` cache variables; all three are required:

- `LUASF_LUA_ROOT` — Lua source directory containing `src/lua.h`.
- `LUASF_SOL2_ROOT` — sol2 source directory containing `include/sol2/sol.hpp`. LuaSF needs the `sol2` `optional::emplace` fix from `cmake/sol/pr1606.patch`, which the LuaSF repository applies to its own checkout; apply it to yours as well, or your build will not compile.
- `LUASF_SFML_ROOT` — SFML source project. When your project already provides the `sfml-*` targets, LuaSF reuses them instead of adding the directory again.

```cmake
cmake_minimum_required(VERSION 3.21)

project(SFLua LANGUAGES C CXX)

set(LUASF_LUA_STUB_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/stub/LuaSF.d.lua")
set(LUASF_LUA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Lua")
set(LUASF_SOL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sol2")
set(LUASF_SFML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SFML")

add_subdirectory(LuaSF)

add_executable(SFLua main.cpp)

target_compile_features(SFLua PRIVATE cxx_std_17)

target_include_directories(SFLua PRIVATE
    "${LUASF_LUA_ROOT}/src"
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

LuaSF never creates the Lua VM. The host creates the state, opens the standard libraries, registers the LuaSF bindings, and closes the state when it is done. `SCRIPTS_DIR` is baked at build time via `target_compile_definitions` (shown in the CMake examples above), so the path works regardless of the current working directory.

```cpp
#include <cstdio>

#include <LuaSF.hpp>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#ifndef SCRIPTS_DIR
#define SCRIPTS_DIR "."
#endif

int main()
{
    lua_State* L = luaL_newstate();
    if (L == nullptr)
        return 1;

    luaL_openlibs(L);

    int result = 0;
    if (LuaSF_initialize_state(L) != 0 || LuaSF_register(L) != 0)
    {
        std::fprintf(stderr, "Lua error: failed to register the LuaSF bindings\n");
        lua_close(L);
        return 1;
    }

    if (LuaSF_enter_state(L) == 0)
    {
        result = 1;
    }
    else
    {
        if (luaL_dofile(L, SCRIPTS_DIR "/Entry.lua") != LUA_OK)
        {
            std::fprintf(stderr, "Lua error: %s\n", lua_tostring(L, -1));
            result = 1;
        }
        LuaSF_leave_state(L);
    }

    LuaSF_quiesce_state(L);
    LuaSF_shutdown_state(L);
    lua_close(L);
    return result;
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

`callback_codecs.json` describes special callback conversions by semantic C++ alias or an exact function-parameter use site. Consumers should read this generated manifest instead of matching expanded `std::function` signatures, and may validate its canonical types against the adjacent `sfml_api.json` snapshot. In particular, `sf::SoundSource::EffectProcessor` is the only semantic selector for the interleaved-float audio codec; an unrelated alias with the same canonical signature does not opt into that protocol.

### Generic `std::function` callbacks

The generator treats `std::function<R(Args...)>` as a recursive first-class type. A Lua function passed to an ordinary C++ `std::function` parameter is automatically retained in the Lua registry and bridged through the same scalar, enum, string, path, bound value/usertype, `vector`, and `optional` conversions used by normal bindings. Both `void` and value returns are supported when every nested type is safely expressible. This is type-driven: there is no whitelist of SFML function names, so compatible callbacks added by future SFML releases use the same generation path automatically.

Signatures containing a bare pointer, writable reference/out parameter, rvalue reference, pointer or reference return, a native-thread boundary, or another lifetime or synchronization contract that the C++ type alone cannot express require one explicitly selected special codec. The selector must be either a semantic alias or an exact function/parameter/signature use site; zero matches and ambiguous matches fail generation. Lua values are validated at the binding boundary, including whether `nil` is allowed by the selected policy. C++ APIs that return `std::function` are not generated in this release.

### Sound effect callback contract

`sf.SoundSource.EffectProcessor` is exposed as a copying callback with this Lua signature:

```lua
fun(
    inputFrames: number[]|nil,
    inputFrameCount: integer,
    outputFrames: number[],
    outputFrameCount: integer,
    frameChannelCount: integer
): {
    inputFrameCount: integer,
    outputFrameCount: integer,
    outputFrames: number[]?
}
```

Arrays are ordinary copying arrays containing flat interleaved samples at dense, 1-based Lua indices. They are not native buffer views, and the old `.value` / `.capacity` fields are not accepted. LuaSF reuses its input and output scratch tables between calls: it overwrites the complete current input, zeroes the complete current output capacity, and removes stale tail entries when a later block is shorter. These tables are callback-scoped scratch values and must not be retained or accessed after the callback returns. Counts are measured in frames; array lengths are measured in samples. A non-`nil` input array has exactly `inputFrameCount * frameChannelCount` samples. At end of stream, input is `nil` and its frame count is zero. The output array has exactly `outputFrameCount * frameChannelCount` samples before the call.

The callback must return a table containing the consumed `inputFrameCount` and produced `outputFrameCount`, both within the original capacities. If the result omits `outputFrames`, LuaSF copies samples from the possibly modified third argument; if it provides `outputFrames`, that dense array replaces the third argument. The selected output array must cover every produced sample and may not exceed the output capacity. Sparse arrays, non-array keys, invalid counts, overflow, and capacity violations are contract errors.

The audio thread uses the host's non-blocking serialized try-enter and never waits for the Lua VM. Lock contention dry-bypasses only the current block without latching an error: LuaSF copies as many input frames as the output capacity permits, while end of stream remains `nil` plus `0` frames and produces `0` frames. Processing resumes on later blocks after the VM becomes available. Hosts that need uninterrupted stateful processing must give the processor an execution state that is not shared with contending work.

After a successful try-enter, LuaSF pauses automatic GC only when it was running on entry. The RAII guard covers scratch-array population, the protected Lua call, and result parsing/copying, and restores the entry state on normal and exceptional exits; a collector that was already stopped is left stopped. Consequently, allocations made by this bridge cannot trigger automatic GC or finalizers on the audio thread. An effect callback must not explicitly run or restart GC, and must not rely on finalizers for real-time cleanup.

A Lua or protocol error latches the shared processor context, so the current and subsequent blocks use the same dry-bypass fallback. Its fixed-capacity deferred error can be consumed once on a logic thread with `LuaSF_take_deferred_callback_error`; error reporting and cleanup must not be performed from the audio callback itself.

### Callback state and host contract

LuaSF uses the `LUA_RIDX_MAINTHREAD` value as the identity for every registered `lua_State*`, including coroutine threads. Sessions, execution hooks, deferred errors, callback retention, quiescing, and shutdown therefore belong to the main state. Callback conversion automatically registers the currently executing coroutine and keeps it alive until shutdown, so a callback may be created from a coroutine stack and invoked later through the owning main state. If a host wants to pass a newly created coroutine directly to lifecycle or enter/leave APIs before any callback conversion has observed it, the host must first call `LuaSF_initialize_state(coroutine)` while it exclusively owns that VM; otherwise, pass the main state.

The host must serialize every Lua entry point—including `lua_pcall`, `luaL_dofile`, and equivalent APIs—with native callbacks. Wrap each entry with a successful `LuaSF_enter_state` and a matching `LuaSF_leave_state`, or install execution hooks and use that same re-entrant/recursive VM lock consistently for host execution. The effect path intentionally uses only the non-blocking try-enter hook. An effect callback must never invoke a producer-waiting lifecycle operation on its own source, because that operation would wait for the current callback itself.

Before closing a state, stop callback producers and detach or destroy their processors/factories while the state is still valid, then always perform `LuaSF_quiesce_state` → `LuaSF_shutdown_state` → `lua_close`. Quiesce/shutdown is not a substitute for stopping producer threads, and no callback may access the state after shutdown.

## Runtime Notes

LuaSF is built as a dynamic library. At runtime, the executable must be able to load LuaSF itself, the Lua runtime, and the SFML runtime libraries. Use `luasf_copy_runtime_dlls(target)` or `luasf_copy_runtime_files(target)` to copy the required runtime libraries next to your executable.

## License

This project is licensed under the [MIT License](LICENSE).

## Third-Party Licenses

LuaSF does not ship these dependencies; the version table records which variants and releases the build scripts download. Their licenses are:

| Dependency | Version | License |
| --- | --- | --- |
| [SFML](https://www.sfml-dev.org/) | 3.1.0 | [zlib/libpng](https://opensource.org/licenses/Zlib) — see `third_party/SFML/license.md` |
| [SFML-ME](https://github.com/JasonLeon01/SFML-ME) | `310ME-iOSJoystick` tag | [zlib/libpng](https://opensource.org/licenses/Zlib) — see `third_party/SFML/license.md` |
| [SFML-ME-OH](https://github.com/JasonLeon01/SFML-ME) | `310-ME-OH-GLESVER` tag | [zlib/libpng](https://opensource.org/licenses/Zlib) — see `third_party/SFML/license.md` |
| [Lua](https://www.lua.org/) | 5.5.0 | [MIT](https://www.lua.org/license.html) |
| [sol2](https://github.com/ThePhD/sol2) | 3.3.0 | [MIT](https://github.com/ThePhD/sol2/blob/develop/LICENSE.txt) |

SFML may also redistribute external libraries under their own licenses; see the SFML documentation and `third_party/SFML/license.md` for details.
