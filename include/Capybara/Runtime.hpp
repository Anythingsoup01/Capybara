#pragma once
#include <stdexcept>


typedef void CapybaraVariable;

#include <string>
#include <vector>
#include <unordered_map>

#include <dlfcn.h>


using GenericFn = void(*)();

enum class MethodKind { GLOBAL, CLASS_INSTANCE };

enum class ValueType
{
    INT,
    FLOAT,
    POINTER,
    VOID,
};

struct RuntimeValue
{
    ValueType Type;
    union {
        int i;
        float f;
        void* p;
    };

    RuntimeValue() : Type(ValueType::VOID), p(nullptr) {}
    RuntimeValue(int val) : Type(ValueType::INT), i(val) {}
    RuntimeValue(float val) : Type(ValueType::FLOAT), f(val) {}
    RuntimeValue(const char* val) : Type(ValueType::POINTER), p((void*)val) {}
    RuntimeValue(void* val) : Type(ValueType::POINTER), p(val) {}

    template<typename T>
    T As() const
    {
        throw std::runtime_error("Unsupported Conversion!");
    }


};


struct CapyClass;

struct CapyObject
{
    CapyClass* Type;
};

struct ManagedString : CapyObject
{
    std::string Value;
    ManagedString(CapyClass* t, std::string v)
    {
        Type = t;
        Value = std::move(v);
    }
};


struct MethodEntry
{
    std::string Name;
    GenericFn Fn;
    bool External;
    ValueType ReturnType;
    std::vector<ValueType> ParamTypes;
};

struct DeclaredMethodEntry
{
    std::string Name;
    void (*Fn)(CapyObject*);
};

// Type Object, ManagedString, Int32
struct CapyClass
{
    std::string Name;
    CapyClass* Parent;
    unsigned int InstanceSize;
    std::vector<MethodEntry> VTable;
    std::vector<DeclaredMethodEntry> DeclaredMethods;


    CapyClass(const std::string& name, CapyClass* parent, size_t size)
        : Name(name), Parent(parent), InstanceSize(size), VTable(), DeclaredMethods() {}
};



struct CoreTypeRegistry
{
    CapyClass* Object;
    CapyClass* String;
    CapyClass* Int32;
    CapyClass* Array;
    std::vector<CapyClass*> Customs;
};

struct Parameter
{
    std::string ParameterType;
    std::string ParameterName;
};

struct Symbol
{
    std::string DemangledName;
    MethodKind Kind;
    std::string ReturnType;
    std::vector<Parameter> Parameters;
};
