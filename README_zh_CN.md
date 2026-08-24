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

- `LuaSF-source.{tar.gz|zip}` — 来自 `output/` 的生成源码工程，包含 `callback_codecs.json`，不含 `bin/` 和 `build/`
- `LuaSF-embedded-{OS}-{ARCH}-{COMPILER}.{tar.gz|zip}` — 嵌入式包，包含 `callback_codecs.json` 及其匹配的 `sfml_api.json` 校验快照
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

    int result = 0;
    if (LuaSF_enter_state(L) == 0)
    {
        result = 1;
    }
    else
    {
        if (luaL_dofile(L, SCRIPTS_DIR "/Entry.lua") != LUA_OK)
        {
            fprintf(stderr, "Lua error: %s\n", lua_tostring(L, -1));
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
- `LUASF_CALLBACK_CODECS_FILE`：指向 schema v1 `callback_codecs.json` manifest 的绝对路径。
- `LUASF_RUNTIME_FILES`：运行时需要放到可执行文件旁边的打包文件。
- `LUASF_RUNTIME_DLLS`：`LUASF_RUNTIME_FILES` 的兼容别名。
- `luasf_copy_runtime_files(target)`：用于复制所有打包运行时文件的 post-build helper。
- `luasf_copy_runtime_dlls(target)`：`luasf_copy_runtime_files(target)` 的兼容别名。

生成的 `.d.lua` 是全局声明文件，以 `---@meta` 开头；EmmyLua 可从独立 stub 库目录中暴露其中的 `sf` API。

`callback_codecs.json` 通过语义 C++ alias 或精确的函数参数使用点描述特殊 callback 转换。消费方应读取这份生成 manifest，而不是匹配展开后的 `std::function` 签名，并可用相邻的 `sfml_api.json` 快照严格校验 canonical type。特别是，交错 float 音频 codec 只由 `sf::SoundSource::EffectProcessor` 这一语义 selector 选择；具有相同 canonical 签名的无关 alias 不会进入该协议。

### 通用 `std::function` callback

生成器将 `std::function<R(Args...)>` 作为可递归的一等类型处理。传给普通 C++ `std::function` 参数的 Lua function 会自动保存在 Lua registry 中，并复用普通绑定的标量、枚举、字符串、path、已绑定 value/usertype、`vector` 和 `optional` 转换。只要所有子类型都能安全表达，就同时支持 `void` 和普通值返回。该流程完全由类型驱动，没有 SFML 函数名白名单；未来 SFML 新增的兼容 callback 会自动走同一生成路径。

如果签名包含裸指针、可写引用/out 参数、rvalue reference、指针或引用返回值、native thread 边界，或者 C++ 类型本身无法表达的生命周期及同步约束，就必须显式选择且唯一命中一个特殊 codec。selector 只能是语义 alias，或精确的函数/参数/signature 使用点；零命中或多重命中都会令生成失败。Lua 值会在绑定调用边界立即验证，包括所选策略是否允许 `nil`。本版本仍不生成返回 `std::function` 的 C++ API。

### 音效回调契约

`sf.SoundSource.EffectProcessor` 使用复制协议，Lua 签名如下：

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

数组按 Lua 的 1-based 索引保存 flat interleaved sample。count 的单位是 frame，数组长度的单位是 sample。非 `nil` 输入数组必须恰好包含 `inputFrameCount * frameChannelCount` 个 sample；流结束时 input 为 `nil` 且输入 frame count 必须为零。调用前，输出数组会按 `outputFrameCount * frameChannelCount` 个 sample 零初始化。

callback 必须返回 table，其中包含实际消费的 `inputFrameCount` 和实际产生的 `outputFrameCount`，且二者不能超过原始容量。若结果省略 `outputFrames`，LuaSF 会回拷可能已被修改的第三个参数；若提供 `outputFrames`，则改用该 dense 数组。选中的输出数组必须覆盖所有实际输出 sample，且不能超过输出容量。稀疏数组、非数组 key、非法 count、溢出和容量越界均属于协议错误。

音频线程只尝试进入 Lua state，不会等待。锁竞争只会旁路当前 block，且不会锁存错误；锁释放后会恢复处理。Lua 或协议错误会锁存共享 processor context，使当前及后续 block 均旁路；逻辑线程可通过 `LuaSF_take_deferred_callback_error` 仅取出一次固定容量的延迟错误。

### Callback state 与宿主契约

LuaSF 以 `LUA_RIDX_MAINTHREAD` 作为每个已登记 `lua_State*`（包括 coroutine thread）的统一 identity。session、execution hook、deferred error、callback 保活、quiesce 和 shutdown 都归属 main state。callback 转换会自动登记当前正在执行的 coroutine，并将其强保活到 shutdown，因此 callback 可以在 coroutine 栈上创建 registry 引用，之后通过所属 main state 安全调用。如果宿主要在 callback 转换尚未观察到新 coroutine 前，直接把它传给 lifecycle 或 enter/leave API，必须先在独占该 VM 时调用 `LuaSF_initialize_state(coroutine)`；否则应传 main state。

宿主必须让所有 Lua 入口（包括 `lua_pcall`、`luaL_dofile` 及同类 API）与 native callback 串行化。每个入口都应在成功的 `LuaSF_enter_state` 后执行并匹配 `LuaSF_leave_state`，或者安装 execution hooks，并让宿主执行始终使用同一把可重入/递归 VM lock。关闭 state 前必须先停止 callback producer，并严格执行 `LuaSF_quiesce_state` → `LuaSF_shutdown_state` → `lua_close`；shutdown 之后 callback 不得再访问该 state。

## 运行时说明

LuaSF 会被构建为动态库。运行时，可执行程序必须能够加载 LuaSF 本身、Lua 运行时和 SFML 运行时库。可以使用 `luasf_copy_runtime_dlls(target)` 或 `luasf_copy_runtime_files(target)` 将所需运行时库复制到可执行文件旁边。

## 许可证

本项目采用 [MIT License](LICENSE)。

## Third-Party Licenses

打包依赖的版本记录在 `versions.conf` 中，其许可证如下：

| 依赖 | 版本 | 许可证 |
| --- | --- | --- |
| [SFML-ME](https://github.com/JasonLeon01/SFML-ME/tree/310ME) | `310ME` 分支 | [zlib/libpng](https://opensource.org/licenses/Zlib) — 详见 `third_party/SFML/license.md` |
| [Lua](https://www.lua.org/) | 5.5.0 | [MIT](https://www.lua.org/license.html) |
| [sol2](https://github.com/ThePhD/sol2) | 3.3.0 | [MIT](https://github.com/ThePhD/sol2/blob/develop/LICENSE.txt) |

SFML 还可能附带其他外部库，这些库遵循各自的许可证；详见 SFML 文档以及 `third_party/SFML/license.md`。
