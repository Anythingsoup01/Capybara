#pragma once

#include <unordered_map>

struct SymbolMetaData {
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

// This function will let you convert a string into the relative
// value type, this returns ValueType::VOID if there is no match
ValueType string_to_value_type(const std::string &value);

// This function will return the corresponding size of the ValueType
size_t type_size(ValueType type);

struct RuntimeValue
{
    template<typename T>
    RuntimeValue(T value)
    {
        memset(m_Data, 0, sizeof(m_Data));
        static_assert(sizeof(T) <= 128, "type too large");
        memcpy(m_Data, &value, sizeof(T));

    }

    void* raw_ptr() { return m_Data; }
    const void* raw_ptr() const { return m_Data; }

private:
    char m_Data[128];
};

struct CapyField
{
    void* SymHandle;
    ValueType FieldType;
    unsigned int Offset;
    bool ClassMember;

    // We use a string for custom types
    // that are either non-standard or
    // user made
    std::string FieldTypeString;

    std::vector<uint8_t> DefaultData;
};

struct CapyMethod
{
    void* SymHandle;
    ValueType ReturnType;
    std::vector<ValueType> Parameters;
    bool ClassMember;
};

struct CapyVTable
{
    std::unordered_map<std::string, std::unique_ptr<CapyMethod>> Methods;
    std::unordered_map<std::string, std::unique_ptr<CapyField>> Fields;
};

struct CapyClass {
    //                  Name
    std::unordered_map<std::string, SymbolMetaData> SymbolMetaDatas;
    std::unique_ptr<CapyVTable> VTable;
    std::string NameSpace;
    std::string ClassName;
};

struct CapyImage {
    std::unordered_map<std::string, std::unique_ptr<CapyClass>> Classes;
};

struct CapyLibrary {
    std::unique_ptr<CapyImage> Image;
    std::filesystem::path LibPath;
    void* SymbolInstance;
    bool IsCore;


    // Map of relocations
    std::unordered_map<uintptr_t, uintptr_t> OriginalRelocs;

    CapyLibrary(std::unique_ptr<CapyImage> image)
        : Image(std::move(image)) {}

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

// TODO: Delete this if not needed
template<typename... Ts>
struct capy_type_list {};

using FieldSetterFunc = void(*)(void* ptr, void* value);

struct Storage {
    std::unordered_map<std::string, std::unique_ptr<CapyDomain>> Domains;
    std::vector<std::string> KnownClassNames;
    std::vector<std::string> IgnoredNamespaces;
    std::vector<std::string> IgnoredClassNames;
    std::vector<std::string> IgnoredNames;
    std::unordered_map<std::string, void*> InternalCalls;
    std::unordered_map<std::string, FieldSetterFunc> TypeSetters;
    bool IgnoreEmptyNamespaces;
    std::filesystem::path SearchPath;
};
