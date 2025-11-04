#include "cpypch.h"
#include "Runtime.hpp"

template<>
int RuntimeValue::As<int>() const
{
    if (Type != ValueType::INT32) throw std::runtime_error("Type mismatch for int");
    return i;
}

template<>
float RuntimeValue::As<float>() const
{
    if (Type != ValueType::FLOAT) throw std::runtime_error("Type mismatch for float");
    return f;
}

template<>
const char* RuntimeValue::As<const char*>() const
{
    if (Type != ValueType::POINTER) throw std::runtime_error("Type mismatch for string");
    return (const char*)p;
}

template<>
void* RuntimeValue::As<void*>() const
{
    if (Type != ValueType::POINTER) throw std::runtime_error("Type mismatch for object");
    return p;
}
