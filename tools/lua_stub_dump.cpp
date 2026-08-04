#include <iostream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
using WriteStubFn = int (*)(const char*);

class SharedLibrary
{
public:
    explicit SharedLibrary(const char* path)
    {
#if defined(_WIN32)
        handle_ = LoadLibraryA(path);
#else
        handle_ = dlopen(path, RTLD_NOW);
#endif
    }

    ~SharedLibrary()
    {
#if defined(_WIN32)
        if (handle_ != nullptr)
            FreeLibrary(static_cast<HMODULE>(handle_));
#else
        if (handle_ != nullptr)
            dlclose(handle_);
#endif
    }

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    [[nodiscard]] bool valid() const
    {
        return handle_ != nullptr;
    }

    [[nodiscard]] WriteStubFn write_stub_function(const char* symbol) const
    {
#if defined(_WIN32)
        return reinterpret_cast<WriteStubFn>(GetProcAddress(static_cast<HMODULE>(handle_), symbol));
#else
        return reinterpret_cast<WriteStubFn>(dlsym(handle_, symbol));
#endif
    }

    [[nodiscard]] static std::string last_error()
    {
#if defined(_WIN32)
        const DWORD error_code = GetLastError();
        if (error_code == 0)
            return {};

        char* message = nullptr;
        const DWORD size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error_code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<char*>(&message),
            0,
            nullptr);

        std::string result = size == 0 ? std::string{} : std::string(message, size);
        if (message != nullptr)
            LocalFree(message);
        return result;
#else
        const char* message = dlerror();
        return message == nullptr ? std::string{} : std::string(message);
#endif
    }

private:
#if defined(_WIN32)
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: lua_stub_dump <module-library> <write-stub-symbol> <output.d.lua>\n";
        return 2;
    }

    SharedLibrary module(argv[1]);
    if (!module.valid())
    {
        std::cerr << "failed to load LuaSF module: " << argv[1] << "\n";
        const std::string error = SharedLibrary::last_error();
        if (!error.empty())
            std::cerr << error << "\n";
        return 1;
    }

    const WriteStubFn write_stub = module.write_stub_function(argv[2]);
    if (write_stub == nullptr)
    {
        std::cerr << "failed to find LuaSF stub writer symbol: " << argv[2] << "\n";
        const std::string error = SharedLibrary::last_error();
        if (!error.empty())
            std::cerr << error << "\n";
        return 1;
    }

    const int result = write_stub(argv[3]);
    if (result != 0)
        std::cerr << "failed to write LuaSF stub: " << argv[3] << "\n";
    return result;
}
