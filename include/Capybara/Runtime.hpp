#pragma once
#include <stdexcept>


typedef void CapybaraVariable;

#include <string>
#include <vector>

enum class ValueType 
{
    INT,
    FLOAT,
    STRING,
    OBJECT,
    VOID,
};

struct RuntimeValue
{
    ValueType Type;
    union {
        int i;
        float f;
        void* obj;
    };
    std::string s;

    RuntimeValue() : Type(ValueType::VOID), obj(nullptr) {}
    RuntimeValue(int val) : Type(ValueType::INT), i(val) {}
    RuntimeValue(float val) : Type(ValueType::FLOAT), f(val) {}
    RuntimeValue(const std::string& val) : Type(ValueType::STRING), s(val) {}
    RuntimeValue(void* val) : Type(ValueType::OBJECT), obj(val) {}

    template<typename T>
    T As() const
    {
        throw std::runtime_error("Unsupported Conversion!");
    }


};

template<>
int RuntimeValue::As<int>() const
{
    if (Type != ValueType::INT) throw std::runtime_error("Type mismatch for int");
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
    if (Type != ValueType::STRING) throw std::runtime_error("Type mismatch for string");
    return s.c_str();
}

template<>
void* RuntimeValue::As<void*>() const
{
    if (Type != ValueType::OBJECT) throw std::runtime_error("Type mismatch for object");
    return obj;
}

struct CapyType;

struct CapyObject
{
    std::string LibraryName;
    CapyType* Type;
};

struct ManagedString : CapyObject
{
    std::string Value;
    ManagedString(CapyType* t, std::string v)
    {
        Type = t;
        Value = std::move(v);
    }
};

struct FunctionEntry
{
    void* (*fn)(...);
    std::vector<ValueType> ParamTypes;
    ValueType ReturnType;
};

struct MethodEntry
{
    std::string Name;
    void* (*fn)(CapyObject*, ...);
    FunctionEntry Target;
};

struct DeclaredMethodEntry
{
    std::string Name;
    void (*fn)(CapyObject*);
};


struct CapyType
{
    std::string Name;
    CapyType* Parent;
    unsigned int InstanceSize;
    std::vector<MethodEntry> VTable;
    std::vector<DeclaredMethodEntry> DeclaredMethods;


    CapyType(const std::string& name, CapyType* parent, size_t size)
        : Name(name), Parent(parent), InstanceSize(size), VTable(), DeclaredMethods() {}
};



struct CoreTypeRegistry
{
    CapyType* Object;
    CapyType* String;
    CapyType* Int32;
    CapyType* Array;
    std::vector<CapyType*> Customs;
};
