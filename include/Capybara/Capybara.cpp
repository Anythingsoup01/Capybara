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

    void* GetFFIArgPtr(const RuntimeValue& val)
    {
        switch (val.Type) 
        {
            case ValueType::INT: return (void*)&val.i;
            case ValueType::FLOAT: return (void*)&val.f;
            case ValueType::STRING: return (void*)val.s.c_str();
            case ValueType::OBJECT: return val.obj;
            default: return nullptr;
        }
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
    void* instance = dlmopen(LM_ID_NEWLM, filePath.c_str(), RTLD_LAZY | RTLD_LOCAL);

    if (!instance)
    {
        dlclose(instance);
        std::cerr << "ERROR: " << dlerror() << std::endl;
        return false;
    }
    auto obj = CreateObject();
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
        

        std::string tmp = demangledName;
        size_t firstParenthesi = tmp.find_first_of('(');
        if (firstParenthesi != std::string::npos)
            tmp.erase(firstParenthesi);

        std::cout << tmp << std::endl;

        MethodEntry method = LoadExternalMethod(tmp, func);


        obj->Type->VTable.push_back(method);

    }

    AllocateCapyObject(obj);

    dlclose(instance);
    instance = nullptr;

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

    return CPY_REINTERPRET_VTAB(method->Fn)(obj);
}

MethodEntry Capybara::LoadExternalMethod(const std::string& name, void* handle)
{
    MethodEntry method;
    method.Name = name;
    method.Fn = CPY_REINTERPRET_GEN(handle);
    method.External = true;
    return method;
}

void* Capybara::CallExternalMethod(MethodEntry* method, const std::vector<RuntimeValue>& values)
{
    if (!method || !method->Fn) return nullptr;

    size_t nargs = values.size();
    std::vector<ffi_type*> ffiArgTypes(nargs);
    std::vector<void*> ffiArgValues(nargs);

    for (size_t i = 0; i < nargs; ++i)
    {
        ffiArgTypes[i] = Utils::GetFFIType(method->ParamTypes[i]);
        ffiArgValues[i] = Utils::GetFFIArgPtr(values[i]);
    }
    ffi_cif cif;
    ffi_type* ffiRet = Utils::GetFFIType(method->ReturnType);

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ffiRet, ffiArgTypes.data()) != FFI_OK)
    {
        std::cerr << "FFI prep failed!\n";
        return nullptr;
    }

    union {
        int i;
        float f;
        void* p;
    } ret;

    ffi_call(&cif, FFI_FN(method->Fn), &ret, ffiArgValues.data());

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
    return std::make_unique<CapyObject>(CapyObject{ "Object", s_CoreRegistry.Object });
}

std::unique_ptr<ManagedString> Capybara::CreateManagedString(const std::string& data)
{
    return std::make_unique<ManagedString>(ManagedString{s_CoreRegistry.String, data});
}

void Capybara::SetParameters(CapyObject* obj, const std::string& methodName, const ValueType& returnType, const std::vector<ValueType>& params)
{
    MethodEntry* method = GetMethod(obj, methodName);
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

