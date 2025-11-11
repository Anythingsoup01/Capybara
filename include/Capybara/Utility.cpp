#include "cpypch.h"
#include "Utility.h"

std::string demangle_symbol_name(const char* name)
{
    int status = 0;
    // Call the ABI demangling function
    char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (status == 0) {
        std::string result(demangled);
        free(demangled);
        return result;
    }
    return name;
}

bool strs_n_equal(const std::string& mainString, const std::vector<std::string>& comparedTo)
{
    bool isEqual = false;

    for (auto& check : comparedTo)
    {
        if (strncmp(mainString.c_str(), check.c_str(), check.length()) == 0)
        {
            isEqual = true;
            break;
        }
    }
    return isEqual;
}

// This one checks the length
bool str_n_equal(const std::string& mainString, const std::string& comparedTo)
{
    if (mainString.length() != comparedTo.length())
        return false;

    if (strncmp(mainString.c_str(), comparedTo.c_str(), mainString.length()) == 0)
    {
        return true;
    }

    return false;
}

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

    if (strs_n_equal(value, { "int", "int32_t" }))
        return ValueType::INT32;

    if (strs_n_equal(value, { "float" }))
        return ValueType::FLOAT;

    return ValueType::VOID;
}

ffi_type* get_ffi_type_p(ValueType type)
{
    switch (type)
    {
        case ValueType::INT32: return &ffi_type_sint32;
        case ValueType::FLOAT: return &ffi_type_float;
        case ValueType::POINTER: return &ffi_type_pointer;
        case ValueType::VOID: return &ffi_type_void;
    }

    return &ffi_type_void;
}

void* get_ffi_arg_p(RuntimeValue& val)
{
    switch (val.Type)
    {
        case ValueType::INT32: return &val.i;
        case ValueType::FLOAT: return &val.f;
        case ValueType::POINTER: return &val.p;
        default: return nullptr;
    }
}

size_t type_size(ValueType type)
{
    switch (type)
    {
        case ValueType::FLOAT: return sizeof(float);
        case ValueType::INT32: return sizeof(int32_t);
        case ValueType::POINTER: return sizeof(void*);
        case ValueType::VOID: return 0;
    }

    return 0;
}

// Get the name of a type DIE
std::string get_short_name(const dwarf::die& die)
{
    // fallback: mangled name
    if (die.has(dwarf::DW_AT::name))
        return die[dwarf::DW_AT::name].as_string();
    // fallback: mangled name
    if (die.has(dwarf::DW_AT::linkage_name))
        return die[dwarf::DW_AT::linkage_name].as_string();

    return "<anon>";
}

std::string resolve_type(const dwarf::die& type_die)
{
    auto tag = type_die.tag;

    if (type_die.has(dwarf::DW_AT::name))
        return type_die[dwarf::DW_AT::name].as_string();

    switch(tag) {
        case dwarf::DW_TAG::pointer_type:
            if (type_die.has(dwarf::DW_AT::type))
                return resolve_type(type_die[dwarf::DW_AT::type].as_reference()) + "*";
            return "void*";
        case dwarf::DW_TAG::const_type:
            if (type_die.has(dwarf::DW_AT::type))
                return resolve_type(type_die[dwarf::DW_AT::type].as_reference()) + " const";
            return "const";
        case dwarf::DW_TAG::reference_type:
            if (type_die.has(dwarf::DW_AT::type))
                return resolve_type(type_die[dwarf::DW_AT::type].as_reference()) + "&";
            return "<ref>";
        case dwarf::DW_TAG::rvalue_reference_type:
            if (type_die.has(dwarf::DW_AT::type))
                return resolve_type(type_die[dwarf::DW_AT::type].as_reference()) + "&&";
            return "<rref>";
        default:
            return "<unnamed-type>";
    }
}

std::string get_return_type(const dwarf::die& die)
{
    if (die.has(dwarf::DW_AT::type)) {
        try {
            dwarf::die typeDie = die[dwarf::DW_AT::type].as_reference();
            return resolve_type(typeDie);
        } catch (...) {
            return "void*"; // fallback to generic pointer
        }
    }
    // DWARF omitted return type — assume unknown, default to void*
    return "void*";
}

