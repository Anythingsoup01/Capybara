#pragma once

namespace DLL
{
#   define ADD_INTERNAL_CALL(name, ret, ...) \
        using name##_func_t = ret(*)(__VA_ARGS__); \
        extern name##_func_t name;

#   define INITIALIZE_INTERNAL_CALL(name) \
        name##_func_t name = nullptr;

    ADD_INTERNAL_CALL(Internal_Add, int, int, int);
}
