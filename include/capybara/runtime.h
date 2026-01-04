#pragma once

#include <cstdint>
#include <memory.h>
#include <link.h>

#include "../internal/fswatcher/fswatcher.h"

constexpr size_t INVALID_OFFSET = static_cast<size_t>(-1);

// This is a simple hashing algorithm to let us use uint64_t
// comparisons instead of string comparisons
constexpr uint64_t generate_hash(const std::string_view& str)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : str)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}


enum class StringStorage : uint8_t
{
    Literal,
    Heap,
    Arena,
    Interned
};

// This is a basic managed string utilized by the domain, but can be used basically anywhere
struct CapyString
{
    const char* Data = nullptr;
    uint64_t    Size = 0;
    StringStorage Storage = StringStorage::Literal;

    inline const char* c_str() const { return Data ? Data : ""; }
    inline const size_t length() const { return Size; }

    inline const bool empty() const { return Size == 0 || Data == nullptr; }
    
    bool operator==(const CapyString& rhs) const
    {
        if (this->Storage == StringStorage::Interned &&
                rhs.Storage  == StringStorage::Interned)
        {
            return this->Data == rhs.Data;
        }

        if (this->Size != rhs.Size)
            return false;

        return this->Size == 0 || memcmp(this->Data, rhs.Data, this->Size) == 0;
    }
};

// This is a const char* memory storage for all our items and doesn't allow for comparisons
struct CapyStringArena
{
    char* Base = nullptr;
    size_t Capacity = 0;
    size_t Offset = 0;

    CapyStringArena(size_t size)
    {
        Base = (char*)malloc(size);
        Capacity = size;
        Offset = 0;
    }

    ~CapyStringArena()
    {
        free(Base);
        Base = nullptr;
        Capacity = 0;
        Offset = 0;
    }

    char* alloc(size_t len)
    {
        if (Offset + len > Capacity) return nullptr;
        char* ptr = Base + Offset;
        Offset += len;
        return ptr;
    }

    void reset() { Offset = 0; }
};


// This is for strings we plan to compare, ie field names, and are stored seperately
// so we can store them and their pointers for pointers comparisons instead of strncmp(s)
struct CapyStringTable
{
    std::unordered_map<uint64_t, const char*> Table;

    CapyString intern(const char* s, CapyStringArena& arena)
    {
        uint64_t hash = generate_hash(s);

        auto it = Table.find(hash);
        if (it != Table.end())
        {
            return { it->second, (uint64_t)strlen(s), StringStorage::Interned };
        }

        size_t len = strlen(s) + 1;
        char* buf = arena.alloc(len);
        if (!buf) return { nullptr, 0, StringStorage::Interned };

        memcpy(buf, s, len);
        Table[hash] = buf;

        return { buf, (uint64_t)(len - 1), StringStorage::Interned };
    }

    void clear() { Table.clear(); }
};

// This function retures a managed string literal
CapyString capy_string_literal(const char* s);

// This function returns a heap allocated managed string literal
CapyString capy_string_heap(const char* s);

// This function returns an arena allocated string
CapyString capy_string_arena(CapyStringArena& arena, const char* s);

CapyString capy_string_intern(const CapyString& capyString);
CapyString capy_string_intern(const char* str);


// This function will release the managed string literal,
// Doing this with a Heap string will free the data and shouldn't
// be used if you intend on using the Heap String afterwards
void capy_string_release(CapyString& s);



struct _SymbolMetaData
{
    CapyString Namespace;
    CapyString ClassName;
    CapyString Name;
    CapyString Signature;
    CapyString ReturnType;

    std::vector<CapyString> ParameterTypes;

    bool IsVariable = false;
    bool IsClassInstance = false;
    bool IsStruct = false;
    size_t Offset = INVALID_OFFSET;
};


enum class ValueType
{
    VOID = 0,
    INT8, INT16, INT32, INT64,
    UINT8, UINT16, UINT32, UINT64,
    FLOAT, DOUBLE, LDOUBLE,
    BOOL, CHAR,
    POINTER,
};

template<typename T>
static constexpr ValueType get_value_type()
{
    if constexpr (std::is_same_v<T, int8_t>) return ValueType::INT8;
    if constexpr (std::is_same_v<T, int16_t>) return ValueType::INT16;
    if constexpr (std::is_same_v<T, int32_t>) return ValueType::INT32;
    if constexpr (std::is_same_v<T, int64_t>) return ValueType::INT64;
    if constexpr (std::is_same_v<T, uint8_t>) return ValueType::UINT8;
    if constexpr (std::is_same_v<T, uint16_t>) return ValueType::UINT16;
    if constexpr (std::is_same_v<T, uint64_t>) return ValueType::UINT32;
    if constexpr (std::is_same_v<T, uint64_t>) return ValueType::UINT64;
    if constexpr (std::is_same_v<T, float>) return ValueType::FLOAT;
    if constexpr (std::is_same_v<T, double>) return ValueType::DOUBLE;
    if constexpr (std::is_same_v<T, long double>) return ValueType::LDOUBLE;
    if constexpr (std::is_same_v<T, bool>) return ValueType::BOOL;
    if constexpr (std::is_same_v<T, char8_t>) return ValueType::CHAR;
    if constexpr (std::is_pointer_v<T>) return ValueType::POINTER;
    return ValueType::VOID;
}

// This function will let you convert a string into the relative
// value type, this returns ValueType::VOID if there is no match
ValueType string_to_value_type(const std::string& value);

// This function will return the corresponding size of the ValueType
size_t type_size(ValueType type);


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

struct BaseClass
{
    CapyString NameSpace;
    CapyString ClassName;
};


struct CapyField
{
    CapyString Name;
    void* SymHandle = nullptr;
    ValueType FieldType = ValueType::VOID;
    uint64_t Offset = 0;
    uint64_t Size = 0;
    bool ClassMember = false;
    
    std::unordered_map<uint64_t, SubField> SubFields;

    CapyString FieldTypeString;

    RuntimeValue DefaultData;
    _SymbolMetaData SymbolMetaData;

    CapyField()
      : SymHandle(nullptr),
        FieldType(ValueType::VOID),         // or whatever “invalid” is in your enum
        Offset(0),
        Size(0),
        ClassMember(false),
        FieldTypeString(capy_string_literal("")),
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
    CapyString Name;
    void* SymHandle = nullptr;
    ValueType ReturnType = ValueType::VOID;
    std::vector<ValueType> Parameters;
    bool ClassMember = false;

    _SymbolMetaData SymbolMetaData;

    CapyMethod()
        : SymHandle(nullptr),
        ReturnType(ValueType::VOID),
        Parameters(),
        ClassMember(false),
        SymbolMetaData{}
    {}

    CapyMethod(const CapyMethod& other)
        : SymHandle(nullptr),
          ReturnType(other.ReturnType),
          Parameters(other.Parameters),
          ClassMember(other.ClassMember),
          SymbolMetaData(other.SymbolMetaData)
    {}
};

struct CapyVTable
{
    std::unordered_map<uint64_t, std::unique_ptr<CapyMethod>> Methods;
    std::unordered_map<uint64_t, std::unique_ptr<CapyField>> Fields;

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
    CapyString NameSpace;
    CapyString ClassName;
    std::unique_ptr<CapyVTable> VTable;

    size_t BaseClassSize = 0;
    size_t DeclaredSize = 0;
    size_t ClassSize = 0;
    size_t Allignment = 16;

    uint64_t Hash;

    bool Resolved = false;

    std::unordered_map<uint64_t, SubField> SubFields;
    std::unordered_map<uint64_t, BaseClass> BaseClasses;

    CapyClass()
        : NameSpace(capy_string_literal("")),
        ClassName(capy_string_literal("")),
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

enum class CapyTableType
{
    TypeDef,
    MethodDef,
    FieldDef,
    MaxTableCount,
};

struct CapyTableInfo
{
    std::unordered_map<uint64_t, _SymbolMetaData> Symbols;
};

struct CapyImage {
    std::array<CapyTableInfo, static_cast<uint32_t>(CapyTableType::MaxTableCount)> Tables;
    std::unordered_map<uint64_t, std::unique_ptr<CapyClass>> Classes;
    std::unordered_map<uint64_t, std::unique_ptr<CapyClass>> Structures;



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
    CapyString Name;
    std::unique_ptr<CapyImage> Image;
    std::filesystem::path LibPath;
    void* SymbolInstance;
    bool IsCore;


    // Map of relocations
    std::unordered_map<uintptr_t, uintptr_t> OriginalRelocs;

    CapyLibrary()
        : Name(capy_string_literal("")),
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
    std::unordered_map<uint64_t, std::unique_ptr<CapyLibrary>> Libraries;
    std::vector<CapyString> CoreLibraries;

    CapyStringArena Arena{1024 * 1024};
    CapyStringTable Table;

    std::unordered_map<uint64_t, uint64_t> StoredSizes;
    std::unordered_map<uint64_t, std::unique_ptr<CapyObject>> LiveObjects;

    CapyDomain()
        : Libraries(),
        CoreLibraries(),
        LiveObjects()
    {}
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

    std::unordered_map<uint64_t, void*> InternalCalls;
    std::mutex DomainReloadMutex;
};

struct CapyConfigStorage
{
    bool IgnoreEmptyNamespaces = false;

    std::unordered_map<uint64_t, std::vector<BaseClass>> ClassMap;
    std::unordered_map<uint64_t, std::vector<BaseClass>> StructMap;
    std::unordered_map<uint64_t, BaseClass> CoreDataStructures;
    std::unordered_map<uint64_t, CapyString> KnownClassNames;
    std::unordered_map<uint64_t, CapyString> KnownStructNames;
    std::vector<CapyString> IgnoredNamespaces;
    std::vector<CapyString> IgnoredClassNames;
    std::vector<CapyString> IgnoredNames;
    std::filesystem::path BinaryPath;
};

struct RuntimeStorage
{
    CapyActiveDomain Active;
    CapyConfigStorage Config;
};

extern RuntimeStorage g_Storage;
