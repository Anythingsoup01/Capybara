#include "cpypch.h"
#include "Runtime.hpp"

template<>
int16_t RuntimeValue::As<int16_t>() const
{
    if (Type != ValueType::INT16) throw std::runtime_error("Type mismatch for int");
    return i16;
}
template<>
int32_t RuntimeValue::As<int32_t>() const
{
    if (Type != ValueType::INT32) throw std::runtime_error("Type mismatch for int");
    return i32;
}
template<>
int64_t RuntimeValue::As<int64_t>() const
{
    if (Type != ValueType::INT64) throw std::runtime_error("Type mismatch for int");
    return i64;
}
template<>
uint16_t RuntimeValue::As<uint16_t>() const
{
    if (Type != ValueType::UINT16) throw std::runtime_error("Type mismatch for int");
    return ui16;
}
template<>
uint32_t RuntimeValue::As<uint32_t>() const
{
    if (Type != ValueType::UINT32) throw std::runtime_error("Type mismatch for int");
    return ui32;
}
template<>
uint64_t RuntimeValue::As<uint64_t>() const
{
    if (Type != ValueType::UINT32) throw std::runtime_error("Type mismatch for int");
    return ui64;
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
