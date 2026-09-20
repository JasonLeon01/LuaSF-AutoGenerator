#pragma once

#if defined(_WIN32) && !defined(LUAGLUE_STATIC)
#if defined(LUAGLUE_BUILD)
#define LUAGLUE_API __declspec(dllexport)
#else
#define LUAGLUE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define LUAGLUE_API __attribute__((visibility("default")))
#else
#define LUAGLUE_API
#endif
