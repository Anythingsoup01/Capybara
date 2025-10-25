#include "Capybara.h"
#include "Runtime.hpp"
#include <dlfcn.h>
#include <exception>
#include <link.h>
#include <iostream>
#include <libelf.h>
#include <gelf.h>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <cxxabi.h>
#include <signal.h>
#include <ffi.h>

#include <Runtime.hpp>

#define CPY_API_ASSERT(x, ...) if (!(x)) { printf("ERROR: %s", __VA_ARGS__); raise(SIGTRAP); }

#define CPY_REINTERPRET_DECL(x) reinterpret_cast<void (*)(CapyObject*)>(x)
#define CPY_REINTERPRET_VTAB(x) reinterpret_cast<void*(*)(CapyObject*, ...)>(x)
#define CPY_REINTERPRET_EXT(x) reinterpret_cast<void*(*)(...)>(x)
#define CPY_REINTERPRET_GEN(x) reinterpret_cast<GenericFn>(x)



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




namespace Utils {
    void PrintTypeInfo(CapyType* t)
    {
        std::cout << "Type: " << t->Name << "\n";
        if (t->Parent)
                std::cout << "  Inherits: " << t->Parent->Name << "\n";

        if (!t->VTable.empty()) {
            std::cout << "  Methods:\n";
            for (auto& m : t->VTable)
                std::cout << "    - " << m.Name << "()\n";
        }
        std::cout << std::endl;
    }

    inline std::string Trim(const std::string& str)
    {
        size_t a = str.find_first_not_of(" \t\n\r");
        if (a == std::string::npos) return nullptr;
        size_t b = str.find_last_not_of(" \t\n\r");
        return str.substr(a, b - a + 1);
    }

    ffi_type* GetFFIType(ValueType type)
    {
        switch (type) 
        {
            case ValueType::INT: return &ffi_type_sint32;
            case ValueType::FLOAT: return &ffi_type_float;
            case ValueType::STRING: return &ffi_type_pointer;
            case ValueType::OBJECT: return &ffi_type_pointer;
            case ValueType::VOID: return &ffi_type_void;
        }

        return &ffi_type_void;
    }

    void* GetFFIArgPtr(RuntimeValue& val)
    {
        switch (val.Type) 
        {
            case ValueType::INT: return &val.i;
            case ValueType::FLOAT: return &val.f;
            case ValueType::STRING:
            case ValueType::OBJECT: return &val.obj;
            default: return nullptr;
        }
    }

    bool IsStaticFromMangled(const std::string& mangledName)
    {
        // This only really applies for GCC/Clang cases
        return (mangledName.find("Ev") != std::string::npos) || (mangledName.find("EP") != std::string::npos);
    }
}

std::string demangle(const char* name)
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

static void* ObjectToString(CapyObject* obj)
{
    std::cout << "[Object of type " << (obj->Type ? obj->Type->Name : "<null>") << "]\n";
    return nullptr;
}

static void* StringToString(CapyObject* obj)
{
    ManagedString* s = static_cast<ManagedString*>(obj);
    std::cout << "[Object of type " << s->Type->Name << "]\n";
    return nullptr;
}

static void* StringGetValue(CapyObject* obj)
{
    ManagedString* s = static_cast<ManagedString*>(obj);
    return reinterpret_cast<void*>(&s->Value);
}


void Capybara::InitCapy()
{
    s_CoreRegistry = CoreTypeRegistry();
    CapyType* Type_Object = new CapyType("Object", nullptr, sizeof(CapyObject));
    CapyType* Type_String = new CapyType("String", Type_Object, sizeof(ManagedString));
    CapyType* Type_Int32 = new CapyType("Int32", Type_Object, sizeof(int32_t));

    RegisterMethod(Type_Object, "ToString", CPY_REINTERPRET_DECL(ObjectToString));
    RegisterMethod(Type_String, "ToString", CPY_REINTERPRET_DECL(StringToString));
    RegisterMethod(Type_String, "GetValue", CPY_REINTERPRET_DECL(StringGetValue));

    BuildVTable(Type_Object);
    BuildVTable(Type_String);
    BuildVTable(Type_Int32);

    s_CoreRegistry.Object = Type_Object;
    s_CoreRegistry.String = Type_String;
    s_CoreRegistry.Int32 = Type_Int32;

}

bool Capybara::AddLibrary(const std::filesystem::path& filePath) // TODO: We should take a second argument for flags
{
    void* instance = dlmopen(LM_ID_NEWLM, filePath.c_str(), RTLD_NOW | RTLD_LOCAL);

    if (!instance)
    {
        std::cerr << "ERROR: " << dlerror() << std::endl;
        return false;
    }
    auto obj = CreateObject();
    obj->LibraryHandle = instance;
    obj->LibraryName = filePath.filename().string();

    std::unordered_map<std::string, std::string> symbols = ProcessLibrary(filePath);
    for (const auto& [demangledName, mangledName] : symbols)
    {
        
        void* func = dlsym(instance, mangledName.c_str());
        const char* err = dlerror();
        if (err)
        {
            std::cout << "ERROR: " << err << std::endl;
            continue;
        }
        if (!func)
        {
            continue;
        }

        MethodKind kind = GetMethodKind(demangledName, mangledName);
        
        ValueType retType = ValueType::VOID;
        std::vector<ValueType> paramTypes;
        ParseSignature(demangledName, retType, paramTypes);

        if (kind == MethodKind::CLASS_INSTANCE && !Utils::IsStaticFromMangled(mangledName))
            paramTypes.insert(paramTypes.begin(), ValueType::OBJECT);

        std::string tmp = demangledName;
        size_t firstParenthesi = tmp.find_first_of('(');
        if (firstParenthesi != std::string::npos)
            tmp.erase(firstParenthesi);

        std::cout << tmp << std::endl;

        MethodEntry method = LoadExternalMethod(tmp, func);
        method.ReturnType = retType;
        method.ParamTypes = paramTypes;

        if (!obj->Type)
        {
            std::cout << "ERROR: obj->Type is null!\n";
            return false;
        }

        obj->Type->VTable.push_back(std::move(method));

    }

    AllocateCapyObject(obj);


    return true;
}

void* Capybara::CallMethod(CapyObject* obj, const std::string& methodName, const std::vector<RuntimeValue>& values)
{
    MethodEntry* method = GetMethod(obj, methodName);
    if (!method)
    {
        std::cerr << "Method doesn't exist!\n";
        return nullptr;
    }

    if (method->External)
        return CallExternalMethod(method, values);

    return CPY_REINTERPRET_VTAB(method->Fn)(obj, values.data());
}

MethodEntry Capybara::LoadExternalMethod(const std::string& name, void* handle)
{
    MethodEntry method;
    method.Name = name;
    method.Fn = CPY_REINTERPRET_GEN(handle);
    method.External = true;
    method.ReturnType = ValueType::VOID;
    method.ParamTypes = {};
    return method;
}

void* Capybara::CallExternalMethod(MethodEntry* method, const std::vector<RuntimeValue>& values)
{
    if (!method || !method->Fn) return nullptr;
    if (method->ParamTypes.size() != values.size())
        throw std::runtime_error("Too many arguments provided!");

    size_t nargs = values.size();

    std::vector<ffi_type*> ffiArgTypes(nargs);
    std::vector<void*> ffiArgValues(nargs);
    std::vector<RuntimeValue> localValues = values;

    for (size_t i = 0; i < nargs; ++i)
    {
        ffiArgTypes[i] = Utils::GetFFIType(method->ParamTypes[i]);
        ffiArgValues[i] = Utils::GetFFIArgPtr(localValues[i]);
    }

    ffi_cif cif;
    ffi_type* ffiRet = Utils::GetFFIType(method->ReturnType);

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ffiRet, const_cast<ffi_type**>(ffiArgTypes.data())) != FFI_OK)
    {
        std::cerr << "[CallExternalMethod] ffi_prep_cfi failed\n";
        return nullptr;
    }

    union {
        int i;
        float f;
        void* p;
    } ret;

    ffi_call(&cif, FFI_FN(method->Fn), &ret, const_cast<void**>(ffiArgValues.data()));
    switch(method->ReturnType)
    {
        case ValueType::INT: return new int(ret.i);
        case ValueType::FLOAT: return new float(ret.f);
        case ValueType::STRING: return ret.p;
        case ValueType::OBJECT: return ret.p;
        case ValueType::VOID: return nullptr;
    }
    return nullptr;
}

CapyObject* Capybara::GetObject(const std::string &libName)
{
    for (auto& lib : s_LoadedLibraries)
    {
        if (lib->LibraryName == libName)
            return lib.get();
    }
    return nullptr;
}

void Capybara::AllocateCapyObject(std::unique_ptr<CapyObject>& obj)
{
    s_LoadedLibraries.push_back(std::move(obj));
}

std::unique_ptr<CapyObject> Capybara::CreateObject()
{
    return std::make_unique<CapyObject>(CapyObject{ "Object", nullptr, s_CoreRegistry.Object });
}

std::unique_ptr<ManagedString> Capybara::CreateManagedString(const std::string& data)
{
    return std::make_unique<ManagedString>(ManagedString{s_CoreRegistry.String, data});
}

ValueType Capybara::ParseTokenType(const std::string& token)
{
    std::string t = Utils::Trim(token);

    // Common matches
    if (t.find("int") != std::string::npos && t.find("int64") == std::string::npos) return ValueType::INT;
    if (t.find("float") != std::string::npos || t.find("double") != std::string::npos) return ValueType::FLOAT;

    // const char* or char*
    if (t.find("const char*") != std::string::npos || t.find("char*") != std::string::npos
        || t.find("const char *") != std::string::npos || t.find("char *") != std::string::npos)
        return ValueType::STRING;

    // void
    if (t == "void" || t == "void ") return ValueType::VOID;

    // If token contains "std::string" treat as STRING (caller must handle constructing std::string if needed)
    if (t.find("std::string") != std::string::npos || t.find("basic_string") != std::string::npos)
        return ValueType::STRING;

    // pointers/references -> treat as OBJECT fallback
    if (t.find('*') != std::string::npos || t.find('&') != std::string::npos)
        return ValueType::OBJECT;

    // Fallback to object for user-defined/class types
    return ValueType::OBJECT;
}

void Capybara::ParseSignature(const std::string& demangledSignature, ValueType& outReturn, std::vector<ValueType>& outParams)
{
    // Clear an existing data, i.e garbage data
    outParams.clear();
    outReturn = ValueType::VOID;

    // Find the parameters
    size_t parenOpen = demangledSignature.find("(");
    size_t paremClose = std::string::npos;
    if (parenOpen != std::string::npos)
        paremClose = demangledSignature.find(")", parenOpen);

    // Looking for the return type
    size_t functionNamePos = demangledSignature.rfind("::");
    size_t lastSpace = demangledSignature.find(" ");

    std::string returnToken;
    if (lastSpace != std::string::npos)
        returnToken = demangledSignature.substr(0, lastSpace);
    else if (parenOpen != std::string::npos)
        returnToken = demangledSignature.substr(0, parenOpen);
    else
        returnToken = "void";

    outReturn = ParseTokenType(returnToken);

    // Checking for parameters
    if (parenOpen == std::string::npos || paremClose == std::string::npos || paremClose <= parenOpen + 1)
        return;

    // Putting all parameters into a string
    std::string params = demangledSignature.substr(parenOpen + 1, paremClose - parenOpen - 1);
    size_t pos = 0;
    while (pos < params.size())
    {
        size_t comma = params.find(',', pos);
        std::string token = (comma == std::string::npos) ? params.substr(pos) : params.substr(pos, comma - pos);
        token = Utils::Trim(token);
        if (!token.empty())
            outParams.push_back(ParseTokenType(token));

        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
}

// We use this to sniff out if it's a class function, or just a namespace
// Not foolproof, will probably make a function to set the namespace when
// adding the library so we can instead check against that, but for now
// we will be using it as is
bool Capybara::IsClassMethod(const std::string& demangledSignature) // TODO: Take in a namespace parameter as well
{
    size_t paren = demangledSignature.find("(");
    std::string beforeParen = (paren == std::string::npos) ? demangledSignature : demangledSignature.substr(0, paren);

    beforeParen = Utils::Trim(beforeParen);

    // Split by "::"
    std::vector<std::string> parts;
    size_t pos = 0;
    while (true)
    {
        size_t found = beforeParen.find("::", pos);
        if (found == std::string::npos) break;
        parts.push_back(beforeParen.substr(pos, found - pos));
        pos = found + 2;
    }

    parts.push_back(beforeParen.substr(pos));

    // If it's 1 or 2, it's either global or namespaced
    if (parts.size() <= 1) return false;
    if (parts.size() == 2) return false;

    return true;
}

MethodKind Capybara::GetMethodKind(const std::string& demangled, const std::string& mangled)
{
    if (!IsClassMethod(demangled))
        return MethodKind::GLOBAL;

    if (mangled.find("M") == std::string::npos)
        return MethodKind::CLASS_STATIC;

    return MethodKind::CLASS_INSTANCE;
}

void Capybara::SetParameters(CapyType* type, const std::string& methodName, const ValueType& returnType, const std::vector<ValueType>& params)
{
    MethodEntry* method = GetMethod(type, methodName);
    if (!method)
    {
        std::cerr << "Method doesn't exist\n";
        return;
    }

    if (method->Fn == nullptr)
        throw std::runtime_error("Method has no valid function pointer");

    method->ReturnType = returnType;
    method->ParamTypes = params;
}

std::unordered_map<std::string, std::string> Capybara::ProcessLibrary(const std::filesystem::path& filePath)
{
    if (elf_version(EV_CURRENT) == EV_NONE)
    {
        std::cerr << "ELF library initialization failed." << std::endl;
        return {};
    }

    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0)
    {
        perror("open");
        return {};
    }

    Elf* elf = elf_begin(fd, ELF_C_READ, nullptr);
    if (!elf)
    {
        std::cerr << "elf_begin() failed: " << elf_errmsg(-1) << std::endl;
        close(fd);
        return {};
    }

    std::unordered_map<std::string, std::string> out;

    Elf_Scn* scn = nullptr;
    while ((scn = elf_nextscn(elf, scn)) != nullptr)
    {
        GElf_Shdr shdr;
        if (gelf_getshdr(scn, &shdr) != &shdr) continue;

        if (shdr.sh_type == SHT_DYNSYM)
        {
            Elf_Data* data = elf_getdata(scn, nullptr);
            int symbol_count = shdr.sh_size / shdr.sh_entsize;

            std::cout << "Dynamic symbols for " << filePath << ":" << std::endl;
            for (int i = 0; i < symbol_count; ++i)
            {
                GElf_Sym sym;
                if (gelf_getsym(data, i, &sym) != &sym) continue;

                if (sym.st_shndx == SHN_UNDEF)
                    continue;

                const char* name = elf_strptr(elf, shdr.sh_link, sym.st_name);
                if (name && *name) {
                    out[demangle(name)] = name;
                }
            }
        }
    }

    elf_end(elf);
    close(fd);
    return out;
}

void Capybara::RegisterMethod(CapyType* type, const std::string& name, void (*fn)(CapyObject*))
{
    type->DeclaredMethods.push_back({name, fn});
}

MethodEntry Capybara::ConvertDeclaredToMethod(const DeclaredMethodEntry& d)
{
    return MethodEntry{d.Name, CPY_REINTERPRET_GEN(d.Fn) };
}

void Capybara::BuildVTable(CapyType* type)
{
    type->VTable.clear();

    if (type->Parent)
    {
        if (type->Parent->VTable.empty() && !type->Parent->DeclaredMethods.empty())
            BuildVTable(type->Parent);
        type->VTable = type->Parent->VTable;
    }

    for(auto& decl : type->DeclaredMethods)
    {
        bool overridden = false;
        for (auto& slot : type->VTable)
        {
            if (slot.Name == decl.Name)
            {
                slot.Fn = CPY_REINTERPRET_GEN(decl.Fn);
                overridden = true;
                break;
            }
        }
        if (!overridden)
            type->VTable.push_back(ConvertDeclaredToMethod(decl));
    }
}

MethodEntry* Capybara::GetMethod(CapyObject *obj, const std::string &methodName)
{
    if (!obj || !obj->Type) return nullptr;
    for (auto& method : obj->Type->VTable)
    {
        if (method.Name == methodName)
            return &method;
    }
    return nullptr;
}

MethodEntry* Capybara::GetMethod(CapyType* type, const std::string &methodName)
{
    if (!type) return nullptr;
    for (auto& method : type->VTable)
    {
        if (method.Name == methodName)
            return &method;
    }
    return nullptr;
}

