# LuaSF AutoGenerator

[English](README.md) | 简体中文

LuaSF AutoGenerator 会生成一个基于 CMake 的 Lua 模块，通过 sol2 将 SFML 暴露给 Lua。生成的模块既可以以源码形式接入，也可以以收集后的二进制包形式接入。

生成的 Lua 模块名为 `LuaSF`。在 CMake 中，嵌入式使用方链接 `LuaSF::LuaSF`。

## 环境要求

- 下方消费端示例需要 CMake 3.21 或更新版本。
- 从源码构建 LuaSF 时需要支持 C++20 的编译器。
- 生成脚本需要 Python 3.12 或更新版本。

依赖版本记录在 `versions.conf` 中。

## 构建 LuaSF

首次初始化依赖：

```bat
init.bat
```

```sh
sh init.sh
```

生成并构建 LuaSF CMake 工程：

```bat
build.bat Release
```

```sh
sh build.sh Release
```

这会在 `output/` 下创建生成后的源码工程，并构建嵌入式 LuaSF 动态库、普通 Lua 扩展模块以及 Lua language-server stub。

收集可分发包：

```bat
collect_result.bat Release
```

```sh
sh collect_result.sh Release
```

收集后的包会写入 `output/result/embedded/` 和 `output/result/extension/`。

收集完成后打包可分发 zip：

```bat
pack_result.bat
```

```sh
sh pack_result.sh
```

压缩包会写入 `output/packages/`：

- `LuaSF-source.{tar.gz|zip}` — 来自 `output/` 的生成源码工程，不含 `bin/` 和 `build/`
- `LuaSF-embedded-{OS}-{ARCH}-{COMPILER}.{tar.gz|zip}` — 嵌入式包
- `LuaSF-extension-{OS}-{ARCH}-{COMPILER}.{tar.gz|zip}` — Lua 扩展包

`pack_result.sh` 生成 `.tar.gz` 压缩包，`pack_result.bat` 生成 `.zip` 压缩包。

## 从 CMake 工程使用

支持三种接入方式：

- **包集成**：将 `output/result/embedded/` 复制到你的工程中，例如复制为 `LuaSF/`。
- **源码集成**：将生成的 `output/` 源码工程复制或 vendor 到你的工程中，例如复制为 `LuaSF/`。
- **普通 Lua 扩展**：在 Lua 中引用或复制 `output/result/extension/bin/`，然后运行 `require("LuaSF")`。

嵌入式使用的 CMake 集成目标是 `LuaSF::LuaSF`。普通 Lua 扩展是单独的构建产物。

### 普通 Lua 扩展

当普通 Lua 运行时需要通过 `require("LuaSF")` 加载 LuaSF 时，使用这种方式。

扩展版本由 `LUASF_LUA_EXTENSION` 宏选择。生成的 `LuaSF_lua_extension` CMake target 会定义这个宏并导出 `luaopen_LuaSF`。默认的 `LuaSF` / `LuaSF::LuaSF` target 不定义这个宏，所以在 IDE 中做源码集成时，除非你显式构建扩展 target 或自己定义这个宏，否则默认仍然是嵌入式版本。

收集结果后，将 `output/result/extension/bin/` 加入 `package.cpath`：

```lua
package.cpath = [[path/to/extension/bin/?.dll;]] .. package.cpath
local sf = require("LuaSF")
```

扩展包不打包 `lua.dll`；它由宿主 Lua 运行时加载，并且必须与扩展使用的 Lua ABI 匹配。

### 包集成

当你已经运行过 `collect_result.bat` 或 `collect_result.sh`，并将 `output/result/embedded/` 复制到工程中时，使用这种方式。

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

`luasf_copy_runtime_dlls(SFLua)` 会把包内的 LuaSF、Lua、SFML 和平台运行时库复制到可执行文件输出目录。`luasf_copy_runtime_files(SFLua)` 也可用，它是平台无关的名称。

### 源码集成

当你已经复制或 vendor 了生成的 `output/` 源码工程时，使用这种方式。

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

只有当你的可执行程序需要生成后的 Lua stub 路径，或者会直接调用打包的 Lua C API 时，才需要 `LUASF_LUA_STUB_OUTPUT`、Lua include 目录和 `LuaSF_lua_shared`。如果你的可执行程序只链接 LuaSF 且不 include Lua 头文件，可以省略这些行。`luasf_copy_runtime_dlls(SFLua)` 在源码集成和包集成中都可用。

## C++ 示例

链接 `LuaSF::LuaSF` 后，这个示例会创建一个已注册所有 LuaSF 绑定的 Lua 状态，然后运行 `Scripts/` 文件夹中的 `Entry.lua`。`SCRIPTS_DIR` 会通过上方 CMake 示例中的 `target_compile_definitions` 在构建时写入，因此路径不依赖当前工作目录。

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

使用源码集成时，必须启用上方源码 CMake 示例中的 Lua 头文件 `target_include_directories`，这样才能找到 `<lua.h>` 和 `<lauxlib.h>`。

## Lua 示例

当可执行程序能够加载 LuaSF 模块和运行时库后，Lua 代码可以直接创建并绘制 SFML 对象。这个示例会打开一个窗口，启用 8x 抗锯齿，绘制一个绿色圆形，并在窗口关闭时退出。

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

## 包内 CMake 项

收集后的包会暴露这些 CMake 项：

- `LuaSF::LuaSF`：导入的 LuaSF 动态库 target。
- `LuaSF::Lua`：导入的打包 Lua 动态库 target。
- `LUASF_STUB_FILE`：指向 `stub/LuaSF.d.lua` 的绝对路径。
- `LUASF_RUNTIME_FILES`：运行时需要放到可执行文件旁边的打包文件。
- `LUASF_RUNTIME_DLLS`：`LUASF_RUNTIME_FILES` 的兼容别名。
- `luasf_copy_runtime_files(target)`：用于复制所有打包运行时文件的 post-build helper。
- `luasf_copy_runtime_dlls(target)`：`luasf_copy_runtime_files(target)` 的兼容别名。

生成的 `.d.lua` 是全局声明文件，以 `---@meta` 开头；EmmyLua 可从独立 stub 库目录中暴露其中的 `sf` API。

## 运行时说明

LuaSF 会被构建为动态库。运行时，可执行程序必须能够加载 LuaSF 本身、Lua 运行时和 SFML 运行时库。可以使用 `luasf_copy_runtime_dlls(target)` 或 `luasf_copy_runtime_files(target)` 将所需运行时库复制到可执行文件旁边。

## 许可证

本项目采用 [MIT License](LICENSE)。

## Third-Party Licenses

打包依赖的版本记录在 `versions.conf` 中，其许可证如下：

| 依赖 | 版本 | 许可证 |
| --- | --- | --- |
| [SFML](https://www.sfml-dev.org/) | 3.1.0 | [zlib/libpng](https://opensource.org/licenses/Zlib) — 详见 `third_party/SFML/license.md` |
| [Lua](https://www.lua.org/) | 5.5.0 | [MIT](https://www.lua.org/license.html) |
| [sol2](https://github.com/ThePhD/sol2) | 3.3.0 | [MIT](https://github.com/ThePhD/sol2/blob/develop/LICENSE.txt) |

SFML 还可能附带其他外部库，这些库遵循各自的许可证；详见 SFML 文档以及 `third_party/SFML/license.md`。
