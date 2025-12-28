#pragma once

#include <cstdint>
#include <memory.h>
#include <link.h>

#include "../internal/fswatcher/fswatcher.h"

constexpr size_t INVALID_OFFSET = static_cast<size_t>(-1);

struct _SymbolMetaData
{
    std::string Namespace;
    std::string ClassName;
    std::string Name;
    std::string Signature;
    std::string ReturnType;

    std::vector<std::string> ParameterTypes;

    bool IsVariable = false;
    bool IsClassInstance = false;
    bool IsStruct = false;
    size_t Offset = INVALID_OFFSET;
};


enum class ValueType
{
    VOID = 0,
    INT16, INT32, INT64,
    UINT16, UINT32, UINT64,
    FLOAT, DOUBLE, BOOL,
    POINTER,
};

template<typename T>
static constexpr ValueType get_value_type()
{
    if constexpr (std::is_same_v<T, int16_t>) return ValueType::INT16;
    if constexpr (std::is_same_v<T, int32_t>) return ValueType::INT32;
    if constexpr (std::is_same_v<T, int64_t>) return ValueType::INT64;
    if constexpr (std::is_same_v<T, uint16_t>) return ValueType::UINT16;
    if constexpr (std::is_same_v<T, uint32_t>) return ValueType::UINT32;
    if constexpr (std::is_same_v<T, uint64_t>) return ValueType::UINT64;
    if constexpr (std::is_same_v<T, float>) return ValueType::FLOAT;
    if constexpr (std::is_same_v<T, double>) return ValueType::DOUBLE;
    if constexpr (std::is_pointer_v<T>) return ValueType::POINTER;
    return ValueType::VOID;
}

// This function will let you convert a string into the relative
// value type, this returns ValueType::VOID if there is no match
ValueType string_to_value_type(const std::string& value);

// This function will return the corresponding size of the ValueType
size_t type_size(ValueType type);

// This is a simple hashing algorithm to let us use uint32_t
// comparisons instead of string comparisons
constexpr uint32_t generate_hash(const std::string_view& str)
{
    uint32_t hash = 2166136261u;
    for (char c : str)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

struct alignas(16) RuntimeValue
{
    ValueType Type;

    RuntimeValue() = default;

    template<typename T>
    RuntimeValue(T value)
        : Type(get_value_type<T>())
    {
        memset(m_Data, 0, sizeof(m_Data));
        static_assert(sizeof(T) <= 128, "type too large");
        memcpy(m_Data, &value, sizeof(T));
    }

    template<typename T>
    T GetValue()
    {
        static_assert(sizeof(T) <= 128, "type too large");
        return *(T*)m_Data;
    }

    void* raw_ptr() { return m_Data; }
    const void* raw_ptr() const { return m_Data; }

private:
    char m_Data[128];
};

struct SubField
{
    size_t Size;
    size_t Offset;
};

struct CapyField
{
    void* SymHandle = nullptr;
    ValueType FieldType = ValueType::VOID;
    uint64_t Offset = 0;
    uint64_t Size = 0;
    bool ClassMember = false;
    
    std::unordered_map<uint32_t, SubField> SubFields;

    std::string FieldTypeString = nullptr;

    RuntimeValue DefaultData;
    _SymbolMetaData SymbolMetaData;

    CapyField()
      : SymHandle(nullptr),
        FieldType(ValueType::VOID),         // or whatever “invalid” is in your enum
        Offset(0),
        Size(0),
        ClassMember(false),
        FieldTypeString(""),
        DefaultData{},                      // zero initialize RuntimeValue
        SymbolMetaData{}                    // uses its own default ctor
    {}
    
    CapyField(const CapyField& other)
        : SymHandle(other.SymHandle),
          FieldType(other.FieldType),
          Offset(other.Offset),
          Size(other.Size),
          ClassMember(other.ClassMember),
          FieldTypeString(other.FieldTypeString),
          DefaultData(other.DefaultData),
          SymbolMetaData(other.SymbolMetaData)
    {}
};

struct CapyMethod
{
    void* SymHandle = nullptr;
    ValueType ReturnType = ValueType::VOID;
    std::vector<ValueType> Parameters;
    std::vector<std::string> ParameterTypeStrings;
    bool ClassMember = false;

    _SymbolMetaData SymbolMetaData;

    CapyMethod()
        : SymHandle(nullptr),
        ReturnType(ValueType::VOID),
        Parameters(),
        ParameterTypeStrings(),
        ClassMember(false),
        SymbolMetaData{}
    {}

    CapyMethod(const CapyMethod& other)
        : SymHandle(nullptr),
          ReturnType(other.ReturnType),
          Parameters(other.Parameters),
          ParameterTypeStrings(other.ParameterTypeStrings),
          ClassMember(other.ClassMember),
          SymbolMetaData(other.SymbolMetaData)
    {}
};

struct CapyVTable
{
    std::unordered_map<uint32_t, std::unique_ptr<CapyMethod>> Methods;
    std::unordered_map<uint32_t, std::unique_ptr<CapyField>> Fields;

    CapyVTable()
        : Methods(),
        Fields()
    {}

    CapyVTable(const CapyVTable& other)
    {
        for (const auto& [id, method] : other.Methods)
            Methods[id] = std::make_unique<CapyMethod>(*method);

        for (const auto& [id, field] : other.Fields)
            Fields[id] = std::make_unique<CapyField>(*field);
    }
};

struct CapyClass
{
    std::string NameSpace;
    std::string ClassName;
    std::unique_ptr<CapyVTable> VTable;

    size_t BaseClassSize = 0;
    size_t DeclaredSize = 0;
    size_t ClassSize = 0;
    size_t Allignment = 16;

    bool IsStruct = false;

    CapyClass()
        : NameSpace(""),
        ClassName(""),
        VTable(nullptr)
    {}

    CapyClass(const CapyClass& other)
        : NameSpace(other.NameSpace),
          ClassName(other.ClassName),
          BaseClassSize(other.BaseClassSize),
          ClassSize(other.ClassSize),
          Allignment(other.Allignment)
    {
        if (other.VTable)
            VTable = std::make_unique<CapyVTable>(*other.VTable);
    }
};

struct CapyImage {
    std::unordered_map<uint32_t, std::unique_ptr<CapyClass>> Classes;
    std::unordered_map<uint32_t, std::unique_ptr<CapyClass>> Structures;

    CapyImage()
        : Classes()
    {}

    CapyImage(const CapyImage& other)
    {
        for (const auto& [id, klass] : other.Classes)
            Classes[id] = std::make_unique<CapyClass>(*klass);
    }
};

struct CapyLibrary {
    std::string Name;
    std::unique_ptr<CapyImage> Image;
    std::filesystem::path LibPath;
    void* SymbolInstance;
    bool IsCore;


    // Map of relocations
    std::unordered_map<uintptr_t, uintptr_t> OriginalRelocs;

    CapyLibrary()
        : Name(""),
        Image(nullptr),
        LibPath(),
        SymbolInstance(nullptr),
        IsCore(false),
        OriginalRelocs()
    {}

    CapyLibrary(std::unique_ptr<CapyImage> image)
        : Image(std::move(image)) {}

    CapyLibrary(const CapyLibrary& other)
        : Name(other.Name),
          LibPath(other.LibPath),
          SymbolInstance(nullptr),
          IsCore(other.IsCore),
          OriginalRelocs(other.OriginalRelocs)
    {
        if (other.Image)
            Image = std::make_unique<CapyImage>(*other.Image);
    }

    ~CapyLibrary()
    {
        if (SymbolInstance)
            dlclose(SymbolInstance);
    }
};

struct CapyObject
{
    void* Memory = nullptr;
    CapyClass* Klass = nullptr;

    CapyObject() = default;
    CapyObject(void* memory, CapyClass* klass)
        : Memory(memory), Klass(klass) {}

    CapyObject(const CapyObject&) = delete;
    CapyObject& operator=(const CapyObject&) = delete;

    CapyObject(CapyObject&& other) noexcept
        : Memory(other.Memory), Klass(other.Klass)
    {
        other.Memory = nullptr;
        other.Klass = nullptr;
    }

    CapyObject& operator=(CapyObject&& other) noexcept
    {
        if (this != &other)
        {
            if (Memory)
                std::free(Memory);

            Memory = other.Memory;
            Klass = other.Klass;
            other.Memory = nullptr;
            other.Klass = nullptr;
        }
        return *this;
    }

    ~CapyObject()
    {
        if (Memory)
            std::free(Memory);

        if (Klass)
            Klass = nullptr;
    }
};

struct CapyDomain {
    std::unordered_map<uint32_t, std::unique_ptr<CapyLibrary>> Libraries;
    std::vector<std::string> CoreLibraries;

    std::unordered_map<uint32_t, uint64_t> StoredSizes;
    std::unordered_map<uint32_t, std::unique_ptr<CapyObject>> LiveObjects;

    CapyDomain()
        : Libraries(),
        CoreLibraries(),
        LiveObjects()
    {}
};

struct BaseClasses
{
    std::string NameSpace;
    std::string ClassName;
};

// TODO: Delete this if not needed
template<typename... Ts>
struct capy_type_list {};

struct CapyJITStorage
{
    DirectoryWatcherStorage WatcherStorage;

    std::mutex JitMutex;
    std::vector<std::filesystem::path> PendingFiles;
    std::vector<std::filesystem::path> FilesToCompile;
    std::vector<std::string> CompilationCommands;

    std::atomic<bool> JitCompilationNeeded{false};
    std::atomic<bool> JitRunning{false};
    std::thread JitThread;

    std::filesystem::path CorePath;
    std::vector<std::filesystem::path> CoreBinaryPaths;
};

struct CapyActiveDomain {
    std::unique_ptr<CapyDomain> Runtime;
    CapyJITStorage JITStorage;

    std::unordered_map<uint32_t, void*> InternalCalls;
    std::mutex DomainReloadMutex;
};

struct CapyConfigStorage
{
    bool IgnoreEmptyNamespaces = false;

    std::unordered_map<uint32_t, std::vector<BaseClasses>> ClassMap;
    std::unordered_map<uint32_t, std::vector<BaseClasses>> StructMap;
    std::unordered_map<uint32_t, BaseClasses> CoreDataStructures;
    std::vector<std::string> KnownClassNames;
    std::vector<std::string> KnownStructNames;
    std::vector<std::string> IgnoredNamespaces;
    std::vector<std::string> IgnoredClassNames;
    std::vector<std::string> IgnoredNames;
    std::filesystem::path BinaryPath;
};

struct RuntimeStorage
{
    CapyActiveDomain Active;
    CapyConfigStorage Config;
};

extern RuntimeStorage g_Storage;
