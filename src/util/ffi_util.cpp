#include "util/ffi_util.h"

ffi_type* get_ffi_type_p(ValueType type)
{
    switch (type)
    {
        case ValueType::INT8: return &ffi_type_sint8;
        case ValueType::INT16: return &ffi_type_sint16;
        case ValueType::INT32: return &ffi_type_sint32;
        case ValueType::INT64: return &ffi_type_sint64;
        case ValueType::UINT8: return &ffi_type_uint8;
        case ValueType::UINT16: return &ffi_type_uint16;
        case ValueType::UINT32: return &ffi_type_uint32;
        case ValueType::UINT64: return &ffi_type_uint64;
        case ValueType::FLOAT: return &ffi_type_float;
        case ValueType::DOUBLE: return &ffi_type_double;
        case ValueType::LDOUBLE: return &ffi_type_longdouble;
        case ValueType::BOOL: return &ffi_type_uint32;
        case ValueType::CHAR: return &ffi_type_schar;
        case ValueType::POINTER: return &ffi_type_pointer;
        case ValueType::VOID: return &ffi_type_void;
    }

    return &ffi_type_void;
}

void* get_ffi_arg_p(RuntimeValue& val)
{
    switch (val.Type)
    {
        case ValueType::INT8:
        case ValueType::INT16:
        case ValueType::INT32:
        case ValueType::INT64:
        case ValueType::UINT8:
        case ValueType::UINT16:
        case ValueType::UINT32:
        case ValueType::UINT64:
        case ValueType::FLOAT:
        case ValueType::DOUBLE:
        case ValueType::LDOUBLE:
        case ValueType::BOOL:
        case ValueType::CHAR:
        case ValueType::POINTER:
            return val.raw_ptr();
        case ValueType::VOID:
            return nullptr;
    }
    return nullptr;
}

