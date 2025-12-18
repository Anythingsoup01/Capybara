#include "capybara/runtime.h"
#include "util/string_util.h"

ValueType string_to_value_type(const std::string& value)
{
    // Should probably return a null enum or assert
    if (value.empty())
        return ValueType::VOID;

    if (value.find("*") != std::string::npos)
        return ValueType::POINTER;


    if (strs_n_equal(value, { "void" }))
        return ValueType::VOID;

    if (strs_n_equal(value, { "const std::string", "std::string" }))
        return ValueType::POINTER;

    if (strs_n_equal(value, { "short", "int16_t" }))
        return ValueType::INT16;

    if (strs_n_equal(value, { "int", "int32_t" }))
        return ValueType::INT32;

    if (strs_n_equal(value, { "long", "int64_t" }))
        return ValueType::INT64;

    if (strs_n_equal(value, { "unsigned short", "uint16_t" }))
        return ValueType::UINT16;

    if (strs_n_equal(value, { "unsigned int", "uint32_t" }))
        return ValueType::UINT32;

    if (strs_n_equal(value, { "unsigned long", "uint64_t" }))
        return ValueType::UINT64;

    if (strs_n_equal(value, { "float" }))
        return ValueType::FLOAT;

    if (strs_n_equal(value, { "double" }))
        return ValueType::DOUBLE;

    if (strs_n_equal(value, { "bool" }))
        return ValueType::BOOL;
    
    return ValueType::VOID;
}

size_t type_size(ValueType type)
{
    switch (type)
    {
        case ValueType::INT16: return sizeof(int16_t);
        case ValueType::INT32: return sizeof(int32_t);
        case ValueType::INT64: return sizeof(int64_t);
        case ValueType::UINT16: return sizeof(uint16_t);
        case ValueType::UINT32: return sizeof(uint32_t);
        case ValueType::UINT64: return sizeof(uint64_t);
        case ValueType::FLOAT: return sizeof(float);
        case ValueType::DOUBLE: return sizeof(double);
        case ValueType::BOOL: return sizeof(bool);
        case ValueType::POINTER: return sizeof(void*);
        case ValueType::VOID: return 0;
    }

    return 0;
}
