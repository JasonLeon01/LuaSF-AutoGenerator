# LuaGlue

LuaGlue 是面向自动生成接口的 C++20 / Lua 5.5 绑定运行时，只依赖 Lua，支持关闭 RTTI。类型注册、重载分派、属性与 userdata 所有权位于编译后的运行时中，模板只适配确定的 C++ 签名。

[English](README.md)

## CMake 接入

可将此目录作为 submodule 或源码依赖，并在添加工程前指定已有的 Lua target：

```cmake
set(LUAGLUE_LUA_TARGET MyLuaTarget)
add_subdirectory(third_party/LuaGlue)
target_link_libraries(MyApplication PRIVATE LuaGlue::LuaGlue)
```

`LUAGLUE_LUA_TARGET` 为空时，项目自带的 finder 查找 Lua 5.5 并校验头文件版本，兼容尚不能识别 Lua 5.5 数字版本宏的 CMake。可显式设置 `LUA_INCLUDE_DIR`、`LUA_LIBRARY`，或通过 `CMAKE_PREFIX_PATH` 指定安装前缀，不会下载 Lua。`LUAGLUE_BUILD_SHARED` 在桌面默认 `ON`，在 iOS、Android、OHOS 默认 `OFF`。同一进程中的模块必须使用同一套 Lua 与 LuaGlue 运行时；桌面分发需携带共享库。

当前支持 `add_subdirectory` 创建的 `LuaGlue::LuaGlue` target，尚未提供安装后的 `find_package(LuaGlue)` 导出包。

## 固定接口绑定

[英文页中的完整示例](README.md#binding-a-fixed-interface) 演示 `BindClass`、`BindConstructor`、`BindAttr`、`BindStaticAttr`、`BindMethod` 与 `BindFunction`。接口显式接收 Lua 名称，成员函数重载应由生成器选择准确的 C++ 签名。

构造器通过 `MyClass.new(...)` 调用；只注册类型不会开放构造。实例属性访问实际实例，静态属性访问 C++ 静态存储，const 属性只读。容器按值转换为普通 Lua table；整体属性赋值会转换容器并写回。

`BindCallable`、`BindFactory` 接受有捕获与无捕获的 lambda，每个重载独立注册到同一个 Lua 名称。`BindProperty` 接收 getter 与可选的 setter，`BindMetamethod` 接收 `"__add"` 等 Lua 元方法名称。`BindBase<Derived, Base>` 注册基类指针转换与 Lua 成员继承。只需要原生接口转换时，使用 `BindCast<Derived, Base>`；它不暴露 Lua 继承，也不要求接口具有 Lua 类型描述。`BindEnum<Enum>` 接收名称与枚举值的初始化列表。

`Defaults{...}` 只表示尾部默认参数。缺少实参时应用默认值，显式 `nil` 仍由参数 codec 处理。需要每次调用重新求值时，使用 `DefaultFactory{[] { return expression; }}`。文档作为 `std::string_view` 绑定选项传入，可与 stub 生成器共用源文件中的 `constexpr std::array`。

可空参数（包括 `const std::optional<T>&`）在省略且没有显式绑定默认值时接收 `nil`；缺少不可空参数仍会导致重载匹配失败。

## 值与生命周期

`BindStruct<T>` 要求启用 `StructTraits<T>`，并添加 `copy()`、`deepcopy()`、`__copy` 与 `__deepcopy`。只有核对包括私有成员在内的完整 C++ 状态具有独立值语义后，才能让 traits 继承 `IndependentValue<T>`；其浅复制与深复制均调用原生拷贝构造。包含资源指针、共享所有权或 Lua 引用的类型，必须提供完整的自定义 `DeepCopy` 策略，或保持为 `BindClass`。

值和智能指针保留所有权；借用指针、引用要求原生拥有者继续存活。`ReturnPolicy::ReferenceInternal` 让返回的 userdata 保活第一个 Lua 参数，实例属性 getter 默认应用此策略。`BindingOptions::keep_alive` 可表达更多关系，其中 `0` 为返回值、`1` 为第一个 Lua 参数。原生指针、共享控制块和类型查询通过公开 API 访问，消费者不应解释 userdata 内存布局。

VM 由宿主创建和关闭。注册前调用 `InitializeState`，通过宿主 execution hooks 与 `ExecutionScope` 串行访问；停止回调生产者后，在 `lua_close` 之前调用 `QuiesceState`、`ShutdownState`。registry 句柄在 shutdown 时脱离 VM，之后不得继续使用。LuaGlue 不会自动让 Lua state 支持并发访问。

`Object`、`Table`、`Function`、`StateView`、`Arguments` 与 `CallResult` 提供 registry 保活值、表操作、栈视图与受保护调用；`CallResult` 保留 nil 返回值的位置。应使用受保护调用，并让 LuaGlue 在分派边界转换 C++ 错误；直接调用 `lua_error` 可能跳过 C++ 析构。

LuaGlue 使用 [MIT 许可证](LICENSE)。
