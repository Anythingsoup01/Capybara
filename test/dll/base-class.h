#pragma once

#define INTERNAL_CALL(name, ret, ...) \
    using name##_func_t = ret(*)(__VA_ARGS__); \
    extern name##_func_t name;

INTERNAL_CALL(Internal_Add, int, int, int);

namespace DLL
{
    class Base
    {
    public:
        Base();
        virtual void Print(const char* msg) = 0;
        virtual void PrintAgain() = 0;
    };
}
