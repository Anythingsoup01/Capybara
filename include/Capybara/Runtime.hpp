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
    INT32,
    FLOAT,
    POINTER,
};

struct RuntimeValue {
    ValueType Type;
    union {
        int i;
        float f;
        void* p;
    };

    RuntimeValue() : Type(ValueType::VOID), p(nullptr) {}
    RuntimeValue(int val) : Type(ValueType::INT32), i(val) {}
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

struct Storage {
    std::unordered_map<std::string, std::unique_ptr<CapyDomain>> Domains;
    std::vector<std::string> KnownClassNames;
    std::vector<std::string> IgnoredNamespaces;
    std::vector<std::string> IgnoredClassNames;
    std::unordered_map<std::string, void*> InternalCalls;
    bool IgnoreEmptyNamespaces;
    std::filesystem::path SearchPath;
};
