#pragma once

#define INTERNAL_CALL(name, ret, ...) \
    using name##_##ret##_func_t = ret(*)(__VA_ARGS__); \
    extern name##_##ret##_func_t name##_##ret;

#define INITIALIZE_INTERNAL_CALL(name, ret) \
    name##_##ret##_func_t name##_##ret = nullptr


#define ADD_INTERNAL_TEMPLATE_CALL(name, ret) template<> ret name(ret a, ret b) { return name##_##ret(a, b); }

namespace DLL
{

    INTERNAL_CALL(Internal_Add, int, int a, int b);
    INTERNAL_CALL(Internal_Add, float, float a, float b);

    template<typename T>
        T Internal_Add(T a, T b);

    class Base
    {
    public:
        Base();
        virtual void Print(const char* msg) = 0;
        virtual void PrintAgain() = 0;
    };
}
