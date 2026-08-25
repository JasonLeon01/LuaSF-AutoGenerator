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

`callback_codecs.json` records the special callback conversion policies used by the generated bindings. Consumers should use `LUASF_CALLBACK_CODECS_FILE` instead of inferring special protocols from expanded `std::function` signatures. The adjacent `sfml_api.json` is the matching API snapshot for strict canonical-type validation.

Ordinary `std::function<R(Args...)>` parameters are bridged automatically when their nested argument and return types use the normal LuaSF value conversions; no SFML function-name whitelist is involved. Bare pointers, writable references/out parameters, native-thread boundaries, and other special lifetime or synchronization contracts require one explicit semantic-alias or exact-use-site codec selector. Lua values, including policy-controlled `nil`, are validated at the binding boundary. C++ APIs returning `std::function` are not generated in this release.

`sf.SoundSource.EffectProcessor` uses ordinary copying arrays of flat interleaved `number[]` values at dense, 1-based indices; native buffer views and the old `.value` / `.capacity` fields are not accepted. Lua receives `inputFrames: number[]|nil`, the input frame capacity, a zero-initialized output-capacity array, the output frame capacity, and the channel count. LuaSF reuses these scratch tables between calls, overwrites or zeroes their complete current ranges, and removes stale tails when a block becomes shorter, so callbacks must not retain them after returning. The callback must return `{ inputFrameCount: integer, outputFrameCount: integer, outputFrames: number[]? }`. Omitting `outputFrames` copies the modified third argument; providing it selects the replacement array. Counts are in frames, array lengths are in samples, arrays must be dense, the selected output must cover all produced samples without exceeding capacity, and end-of-stream input is `nil` with zero input frames.

The audio thread uses the host's non-blocking serialized try-enter and never waits for the Lua VM. Lock contention dry-bypasses only the current block without latching an error, copying as many input frames as output capacity permits; end of stream remains `nil` plus `0` and produces `0`. Hosts that require continuous stateful processing must use an execution state without competing work. After successful entry, LuaSF pauses automatic GC only if it was running, across scratch-array population, the Lua call, and result parsing, then restores the entry state on every exit. A collector already stopped stays stopped, so bridge allocations cannot trigger automatic GC or finalizers on the audio thread. Effect callbacks must not explicitly run/restart GC or rely on finalizers. Lua or protocol faults latch the shared processor into the same fallback and are retrieved once on a logic thread through `LuaSF_take_deferred_callback_error`.

LuaSF uses the owning main state as the identity for registered main-state and coroutine `lua_State*` values. Callback conversion registers and retains the current coroutine automatically; a newly created coroutine passed directly to lifecycle APIs must first be registered with `LuaSF_initialize_state(coroutine)` while the host exclusively owns the VM, or the host should pass the main state. Hosts must serialize every Lua entry such as `lua_pcall` and `luaL_dofile` with native callbacks by using `LuaSF_enter_state`/`LuaSF_leave_state`, or by consistently using installed execution hooks and the same re-entrant/recursive VM lock. The effect path intentionally uses only the non-blocking try-enter hook. An effect callback must never perform a producer-waiting lifecycle operation on its own source, because it would wait for the current callback itself.

Stop callback producers and detach/destroy their processors or factories while the state is still valid, then shut down in the order `LuaSF_quiesce_state` → `LuaSF_shutdown_state` → `lua_close`. Quiesce/shutdown does not stop producer threads, and callbacks must never access a shut-down state.
