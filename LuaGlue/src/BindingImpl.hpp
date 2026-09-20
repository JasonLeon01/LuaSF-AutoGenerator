#pragma once
#include <LuaGlue/Binding.hpp>

namespace lua_glue::detail {
struct OverloadSet {
    std::vector<std::shared_ptr<FunctionRecord>> functions;
    bool unaryMetamethod = false;
};
}  // namespace lua_glue::detail
