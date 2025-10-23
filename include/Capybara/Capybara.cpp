#include "Capybara.h"
#include "Runtime.hpp"
#include <dlfcn.h>
#include <link.h>
#include <iostream>
#include <libelf.h>
#include <gelf.h>
#include <fcntl.h>
#include <unistd.h>
#include <cxxabi.h>
#include <signal.h>

#include <Runtime.hpp>

#define CPY_API_ASSERT(x, ...) if (!(x)) { printf("ERROR: %s", __VA_ARGS__); raise(SIGTRAP); }

#define CPY_REINTERPRET_DECL(x) reinterpret_cast<void (*)(CapyObject*)>(x)
#define CPY_REINTERPRET_VTAB(x) reinterpret_cast<void*(*)(CapyObject*, ...)>(x)
#define CPY_REINTERPRET_RAW(x) reinterpret_cast<void*(*)(...)>(x)

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

void Capybara::AddLibrary(const std::filesystem::path& filePath) // TODO: We should take a second argument for flags
{
    void* instance = dlmopen(LM_ID_NEWLM, filePath.c_str(), RTLD_NOW | RTLD_LOCAL);
    CPY_API_ASSERT(instance, dlerror());


    auto newObj = CreateObject();
    newObj->LibraryName = filePath.filename().string();

    std::unordered_map<std::string, std::string> symbols = ProcessLibrary(filePath);
    for (const auto& [demangledName, mangledName] : symbols)
    {
        
        auto newFunction = CPY_REINTERPRET_RAW(dlsym(instance, mangledName.c_str()));
        const char* err = dlerror();
        if (err)
        {
            std::cout << "ERROR: " << err << std::endl;
            continue;
        }
        if (!newFunction)
        {
            continue;
        }

        std::string tmp = demangledName;
        size_t firstParenthesi = tmp.find_first_of('(');
        tmp.erase(firstParenthesi);

        std::cout << tmp << std::endl;

        newObj->Type->VTable.push_back({tmp, nullptr, newFunction});
    }


    s_LoadedLibraries.push_back({*newObj});

    dlclose(instance);
    instance = nullptr;
}

void* Capybara::CallMethod(CapyObject* obj, const std::string& methodName, const std::vector<RuntimeValue>& values)
{
    CapyType* t = obj->Type;
    for (auto& m : t->VTable) {
        if (m.Name == methodName && (m.fn || m.Target.fn))
        {
            if (m.fn && !m.Target.fn)
            {
                return m.fn(obj);
            }
            else if (!m.fn && m.Target.fn)
            {
                return CallExternalMethod(obj, methodName, values);
            }
            else
            {
                std::cerr << "ERROR: This is some bullshit!\n";
            }
        }
    }
    return nullptr;
}

void* Capybara::CallExternalMethod(CapyObject* obj, const std::string& name, const std::vector<RuntimeValue>& values)
{
    
}

CapyObject* Capybara::GetObject(const std::string &libName)
{
    for (auto& lib : s_LoadedLibraries)
    {
        if (lib.LibraryName == libName)
            return &lib;
    }
    return nullptr;
}

std::unique_ptr<CapyObject> Capybara::CreateObject()
{
    return std::make_unique<CapyObject>(CapyObject{ "Object", s_CoreRegistry.Object });
}

std::unique_ptr<ManagedString> Capybara::CreateManagedString(const std::string& data)
{
    return std::make_unique<ManagedString>(ManagedString{s_CoreRegistry.String, data});
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
    return MethodEntry{d.Name, CPY_REINTERPRET_VTAB(d.fn) };
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
                slot.fn = CPY_REINTERPRET_VTAB(decl.fn);
                overridden = true;
                break;
            }
        }
        if (!overridden)
            type->VTable.push_back(ConvertDeclaredToMethod(decl));
    }
}
