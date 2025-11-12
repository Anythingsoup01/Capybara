#pragma once

#include <unordered_map>
struct Symbol {
    std::string Namespace;
    std::string ClassName;
    std::string Name;
    std::string Signature;
    std::string ReturnType;
    std::vector<std::string> ParameterTypes;
    bool IsVariable = false;
    bool IsClassInstance = false;
    int Offset = -1;
};

enum class ValueType {
    VOID = 0,
    INT16, INT32, INT64,
    UINT16, UINT32, UINT64,
    FLOAT, DOUBLE,
    POINTER,
};

struct RuntimeValue {
    ValueType Type;
    union {
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint16_t ui16;
        uint32_t ui32;
        uint64_t ui64;
        float f;
        void* p;
    };

    RuntimeValue() : Type(ValueType::VOID), p(nullptr) {}
    RuntimeValue(int16_t val) : Type(ValueType::INT16), i16(val) {}
    RuntimeValue(int32_t val) : Type(ValueType::INT32), i32(val) {}
    RuntimeValue(int64_t val) : Type(ValueType::INT64), i64(val) {}
    RuntimeValue(uint16_t val) : Type(ValueType::UINT16), ui16(val) {}
    RuntimeValue(uint32_t val) : Type(ValueType::UINT32), ui32(val) {}
    RuntimeValue(uint64_t val) : Type(ValueType::UINT64), ui64(val) {}
    RuntimeValue(float val) : Type(ValueType::FLOAT), f(val) {}
    RuntimeValue(const char* val) : Type(ValueType::POINTER), p((void*)val) {}
    RuntimeValue(void* val) : Type(ValueType::POINTER), p(val) {}

    template<typename T>
    T As() const
    {
        throw std::runtime_error("Unsupported Conversion!");
    }

};

struct CapyField {
    void* SymHandle;
    ValueType FieldType;
    unsigned int Offset;
    bool ClassMember;
    std::string FieldTypeString;
};

struct CapyMethod {
    void* SymHandle;
    ValueType ReturnType;
    std::vector<ValueType> Parameters;
    bool IsUnresolved;
};

struct CapyVTable {
    std::unordered_map<std::string, std::unique_ptr<CapyMethod>> Methods;
    std::unordered_map<std::string, std::unique_ptr<CapyField>> Fields;
};

struct CapyClass {
    //                  Name
    std::unordered_map<std::string, Symbol> Symbols;
    std::unique_ptr<CapyVTable> VTable;
    std::string NameSpace;
    std::string ClassName;
};

struct CapyImage {
    std::unordered_map<std::string, std::unique_ptr<CapyClass>> Classes;
};

struct CapyLibrary {
    std::unique_ptr<CapyImage> MainImage;
    std::filesystem::path LibPath;
    void* SymbolInstance;
    bool IsCore;


    // Map of relocations
    std::unordered_map<uintptr_t, uintptr_t> OriginalRelocs;

    CapyLibrary(std::unique_ptr<CapyImage> image)
        : MainImage(std::move(image)) {}

    ~CapyLibrary()
    {
        if (SymbolInstance)
            dlclose(SymbolInstance);
    }
};

struct CapyDomain {
    std::unordered_map<std::string, std::unique_ptr<CapyLibrary>> Libraries;
    std::vector<std::string> CoreLibraries;
};

template<typename... Ts>
struct capy_type_list {};


struct Storage {
    std::unordered_map<std::string, std::unique_ptr<CapyDomain>> Domains;
    std::vector<std::string> KnownClassNames;
    std::vector<std::string> IgnoredNamespaces;
    std::vector<std::string> IgnoredClassNames;
    std::unordered_map<std::string, void*> InternalCalls;
    bool IgnoreEmptyNamespaces;
    std::filesystem::path SearchPath;
};
