#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ADD_INTERNAL_CALL(name, ret, ...) \
    using name##_func_t = ret(*)(__VA_ARGS__); \
    extern __attribute__((visibility("default"))) name##_func_t name;

#define INITIALIZE_INTERNAL_CALL(name) \
    __attribute__((visibility("default"))) name##_func_t name = nullptr;

// Declare the internal call
ADD_INTERNAL_CALL(external_adder, void, int, int);

#ifdef __cplusplus
}
#endif
