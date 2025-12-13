#pragma once

#include <cstdint>
#include <memory.h>
#include <link.h>

#include "../internal/fswatcher/fswatcher.h"

constexpr size_t INVALID_OFFSET = static_cast<size_t>(-1);

struct _SymbolMetaData {
    std::string Namespace;
    std::string ClassName;
    std::string Name;
    std::string Signature;
    std::string ReturnType;
    std::vector<std::string> ParameterTypes;
    bool IsVariable = false;
    bool IsClassInstance = false;
    size_t Offset = INVALID_OFFSET;
};

enum class ValueType {
    VOID = 0,
    INT16, INT32, INT64,
    UINT16, UINT32, UINT64,
    FLOAT, DOUBLE,
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
ValueType string_to_value_type(const std::string &value);

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

    RuntimeValue DefaultData;

    _SymbolMetaData SymbolMetaData;

    CapyField()
        : SymHandle(nullptr),
        FieldType(ValueType::VOID),         // or whatever “invalid” is in your enum
        Offset(0),
        ClassMember(false),
        FieldTypeString(""),
        DefaultData{},                      // zero initialize RuntimeValue
        SymbolMetaData{}                    // uses its own default ctor
    {}

    CapyField(const CapyField& other)
    {
        SymHandle        = other.SymHandle;
        FieldType        = other.FieldType;
        Offset           = other.Offset;
        ClassMember      = other.ClassMember;
        FieldTypeString  = other.FieldTypeString;
        DefaultData      = other.DefaultData;     // assumes trivial
        SymbolMetaData   = other.SymbolMetaData;  // assumes trivial/deep copyable
    }
};

struct CapyMethod
{
    void* SymHandle;
    ValueType ReturnType;
    std::vector<ValueType> Parameters;
    std::vector<std::string> ParameterTypeStrings;
    bool ClassMember;

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
    {
        SymHandle             = other.SymHandle;
        ReturnType            = other.ReturnType;
        Parameters            = other.Parameters;
        ParameterTypeStrings  = other.ParameterTypeStrings;
        ClassMember           = other.ClassMember;
        SymbolMetaData        = other.SymbolMetaData;
    }
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
        // Copy methods
        for (const auto& [id, methodPtr] : other.Methods)
        {
            Methods[id] = std::make_unique<CapyMethod>(*methodPtr);
        }

        // Copy fields
        for (const auto& [id, fieldPtr] : other.Fields)
        {
            Fields[id] = std::make_unique<CapyField>(*fieldPtr);
        }
    }
};

struct CapyClass
{
    std::string NameSpace;
    std::string ClassName;

    std::unique_ptr<CapyVTable> VTable;

    CapyClass* Parent;

    CapyClass()
        : NameSpace(""),
        ClassName(""),
        VTable(nullptr),
        Parent(nullptr)
    {}

    CapyClass(const CapyClass& other)
        : NameSpace(other.NameSpace),
        ClassName(other.ClassName),
        Parent(other.Parent)
    {
        if (other.VTable)
            VTable = std::make_unique<CapyVTable>(*other.VTable);
    }
};

struct CapyImage {
    std::unordered_map<uint32_t, std::unique_ptr<CapyClass>> Classes;

    CapyImage()
        : Classes()
    {}

    CapyImage(const CapyImage& other)
    {
        for (const auto& [id, classPtr] : other.Classes)
        {
            Classes[id] = std::make_unique<CapyClass>(*classPtr);
        }
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
        SymbolInstance(other.SymbolInstance),
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

struct CapyDomain {
    std::unordered_map<uint32_t, std::unique_ptr<CapyLibrary>> Libraries;
    std::vector<std::string> CoreLibraries;

    CapyDomain()
        : Libraries(),
        CoreLibraries()
    {}

    CapyDomain(const CapyDomain& other)
        : CoreLibraries(other.CoreLibraries)
    {
        for (const auto& [id, libPtr] : other.Libraries)
        {
            Libraries[id] = std::make_unique<CapyLibrary>(*libPtr);
        }
    }
};

// TODO: Delete this if not needed
template<typename... Ts>
struct capy_type_list {};

using FieldSetterFunc = void(*)(void* ptr, void* value);

struct CapyStorage {
    std::unordered_map<uint32_t, std::unique_ptr<CapyDomain>> Domains;
    std::unordered_map<uint32_t, void*> InternalCalls;
    std::unordered_map<uint32_t, FieldSetterFunc> TypeSetters;



};

struct CapyJITStorage
{
    std::vector<std::unique_ptr<DirectoryWatcher>> FileWatchers;

    std::mutex PendingFileMutex;
    std::vector<std::filesystem::path> PendingFiles;

    std::vector<std::filesystem::path> FilesToCompile;
    std::vector<std::string> CompilationCommands;

    std::atomic<bool> JitCompilationNeeded;
    std::mutex CompilationMutex;
    
    std::atomic<bool> JitRunning = false;
    std::thread JitThread;

    uint32_t JitDomainHash;
    CapyDomain* JitDomain = nullptr;
    std::string DomainName;

    std::vector<std::filesystem::path> CorePaths;
    std::vector<std::filesystem::path> CoreBinaryPaths;

    std::filesystem::path BinaryPath;
};

struct CapyConfigStorage
{
    bool IgnoreEmptyNamespaces = false;

    std::vector<std::string> KnownClassNames;
    std::vector<std::string> IgnoredNamespaces;
    std::vector<std::string> IgnoredClassNames;
    std::vector<std::string> IgnoredNames;

    std::filesystem::path BinaryPath;
};

struct RuntimeStorage
{
    CapyStorage Storage;
    CapyJITStorage JITStorage;

    CapyConfigStorage ConfigStorage;
};

extern RuntimeStorage s_Storage;
