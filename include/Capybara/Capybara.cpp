#include "Capybara.h"
#include "Runtime.hpp"
#include <dlfcn.h>
#include <link.h>
#include <iostream>
#include <fcntl.h>
#include <stdexcept>
#include <cxxabi.h>
#include <signal.h>
#include <ffi.h>

#include <libelfin/elf/elf++.hh>
#include <libelfin/dwarf/dwarf++.hh>
#include <gelf.h>

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
    if (Type != ValueType::POINTER) throw std::runtime_error("Type mismatch for string");
    return (const char*)p;
}

template<>
void* RuntimeValue::As<void*>() const
{
    if (Type != ValueType::POINTER) throw std::runtime_error("Type mismatch for object");
    return p;
}


namespace Utils {

    bool StrNEqual(const std::string& mainString, const std::vector<std::string>& comparedTo)
    {
        bool isEqual = false;

        for (auto& check : comparedTo)
        {
            if (mainString.length() != check.length()) continue;

            if (strncmp(mainString.c_str(), check.c_str(), mainString.length()) == 0)
            {
                isEqual = true;
                break;
            }
        }
        return isEqual;
    }

    void PrintTypeInfo(CapyClass* t)
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

    inline std::string RemoveNamespace(const std::string& str)
    {
        size_t a = str.find_last_of("::");
        if (a == std::string::npos) return str;

        return str.substr(a + 1);
    }

    ffi_type* GetFFIType(ValueType type)
    {
        switch (type)
        {
            case ValueType::INT: return &ffi_type_sint32;
            case ValueType::FLOAT: return &ffi_type_float;
            case ValueType::POINTER: return &ffi_type_pointer;
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
            case ValueType::POINTER: return &val.p;
            default: return nullptr;
        }
    }

    std::string GetShortName(const dwarf::die& die)
    {
        // fallback: mangled name
        if (die.has(dwarf::DW_AT::name))
            return die[dwarf::DW_AT::name].as_string();
        // fallback: mangled name
        if (die.has(dwarf::DW_AT::linkage_name))
            return die[dwarf::DW_AT::linkage_name].as_string();

        if (die.has(dwarf::DW_AT::abstract_origin))
        {
            auto ref = die[dwarf::DW_AT::abstract_origin].as_reference();
            return GetShortName(ref);
        }

        if (die.has(dwarf::DW_AT::specification))
        {
            auto ref = die[dwarf::DW_AT::specification].as_reference();
            return GetShortName(ref);
        }


        return "<anon>";
    }

    std::string ResolveType(const dwarf::die& type_die)
    {
        auto tag = type_die.tag;

        if (type_die.has(dwarf::DW_AT::name))
            return type_die[dwarf::DW_AT::name].as_string();

        switch(tag) {
            case dwarf::DW_TAG::pointer_type:
                if (type_die.has(dwarf::DW_AT::type))
                    return ResolveType(type_die[dwarf::DW_AT::type].as_reference()) + "*";
                return "void*";
            case dwarf::DW_TAG::const_type:
                if (type_die.has(dwarf::DW_AT::type))
                    return ResolveType(type_die[dwarf::DW_AT::type].as_reference()) + " const";
                return "const";
            case dwarf::DW_TAG::reference_type:
                if (type_die.has(dwarf::DW_AT::type))
                    return ResolveType(type_die[dwarf::DW_AT::type].as_reference()) + "&";
                return "<ref>";
            case dwarf::DW_TAG::rvalue_reference_type:
                if (type_die.has(dwarf::DW_AT::type))
                    return ResolveType(type_die[dwarf::DW_AT::type].as_reference()) + "&&";
                return "<rref>";
            default:
                return "<unnamed-type>";
        }
    }

    ValueType StringToValueType(const std::string& value)
    {
        // Should probably return a null enum or assert
        if (value.empty())
            return ValueType::VOID;

        if (value.find("*") != std::string::npos)
            return ValueType::POINTER;


        if (StrNEqual(value, { "void", "void " }))
            return ValueType::VOID;

        if (StrNEqual(value, { "const std::string", "const std::string& ", "const std::string", "std::string", "std::string " }))
            return ValueType::POINTER;

        if (StrNEqual(value, { "int", "int ", "int32_t", "int32_t " }))
            return ValueType::INT;

        if (StrNEqual(value, { "float", "float " }))
            return ValueType::FLOAT;

        return ValueType::VOID;
    }

    std::vector<ValueType> StringsToValueTypes(const std::vector<Parameter>& params)
    {
        // Should probably return a null enum or assert
        if (params.empty())
            return {};

        std::vector<ValueType> out;

        for (auto& [type, name] : params)
        {

            if (type.find("*") != std::string::npos)
                out.push_back(ValueType::POINTER);


            if (StrNEqual(type, { "void", "void " }))
                out.push_back(ValueType::VOID);

            if (StrNEqual(type, { "const std::string", "const std::string& ", "const std::string", "std::string", "std::string " }))
                out.push_back(ValueType::POINTER);

            if (StrNEqual(type, { "int", "int ", "int32_t", "int32_t " }))
                out.push_back(ValueType::INT);

            if (StrNEqual(type, { "float", "float " }))
                out.push_back(ValueType::FLOAT);
        }

        return out;
    }

    std::string GetTypeName(const dwarf::die& typeDie)
    {
        if (!typeDie.valid())
            return "<unnamed>";

        if (typeDie.has(dwarf::DW_AT::name))
            return typeDie[dwarf::DW_AT::name].as_string();

        if (typeDie.has(dwarf::DW_AT::type)) {
            dwarf::die nextType = typeDie[dwarf::DW_AT::type].as_reference();
            return GetTypeName(nextType);
        }

        return "<unnamed>";
    }
    std::string GetReturnType(const dwarf::die& die)
    {
        if (die.has(dwarf::DW_AT::type)) {
            try {
                dwarf::die typeDie = die[dwarf::DW_AT::type].as_reference();
                return ResolveType(typeDie);
            } catch (...) {
                return "void*"; // fallback to generic pointer
            }
        }

        // DWARF omitted return type — assume unknown, default to void*
        return "void*";
    }
    void TraverseAndCollect(const dwarf::dwarf& dw, std::vector<Symbol>& outSymbols)
    {
        for (auto& cu : dw.compilation_units())
        {
            for (auto& die : cu.root())
            {
                if (die.tag != dwarf::DW_TAG::subprogram || !die.has(dwarf::DW_AT::low_pc))
                    continue;

                Symbol sym;
                sym.DemangledName = GetShortName(die);
                sym.Kind = MethodKind::GLOBAL;
                sym.ReturnType = GetReturnType(die);
                // Get function name

                // Get return type

                std::unordered_map<std::string, std::string> paramList;
                for (auto& child : die)
                {
                    if (child.tag != dwarf::DW_TAG::formal_parameter)
                        continue;

                    std::string paramType = ResolveType(child[dwarf::DW_AT::type].as_reference());
                    std::string paramName = GetShortName(child);

                    if (StrNEqual(paramName, { "this" }))
                        sym.Kind = MethodKind::CLASS_INSTANCE;
                    else
                        sym.Parameters.push_back({paramType, paramName});

                }

                outSymbols.push_back(sym);

            }
        }
    }
}

static std::vector<void*> s_Instances = {};

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
    CapyClass* Type_Object = new CapyClass("Object", nullptr, sizeof(CapyObject));
    CapyClass* Type_String = new CapyClass("String", Type_Object, sizeof(ManagedString));
    CapyClass* Type_Int32 = new CapyClass("Int32", Type_Object, sizeof(int32_t));

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

void Capybara::ShutdownCapy()
{
    if (!s_Instances.empty())
    {
        for (auto* instance : s_Instances)
        {
            dlclose(instance);
        }
    }
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

    CapyClass* type = obj->Type;
    //               Mangled Names  SymbolStructure

    

    std::vector<Symbol> symbols = ProcessDwarf(filePath);
    std::unordered_map<std::string, std::string> symbolDeclarations = ProcessLibrary(filePath);
    for (auto& [demangledName, mangledName] : symbolDeclarations)
    {
        void* handle = dlsym(instance, mangledName.c_str());
        if (!handle)
        {
            std::cerr << "ERROR: " << dlerror() << "\n";
            continue;
        }

        Symbol matchedSymbol;

        std::string deNameSpacedName = Utils::RemoveNamespace(demangledName);
        for (auto& sym : symbols)
        {
            std::string comparison = sym.DemangledName + "(";
            bool first = true;
            for (auto& param : sym.Parameters)
            {
                if (!first) comparison += ", ";
                comparison += param.ParameterType;
                first = false;
            }
            comparison += ")";

            if (Utils::StrNEqual(deNameSpacedName, { comparison }))
            {
                matchedSymbol = sym;
                break;
            }
        }

        size_t paren = deNameSpacedName.find('(');
        deNameSpacedName.erase(paren);

        MethodEntry method = LoadExternalMethod(deNameSpacedName, handle);

        if (!matchedSymbol.DemangledName.empty())
        {
            method.ReturnType = Utils::StringToValueType(matchedSymbol.ReturnType);

            method.ParamTypes = Utils::StringsToValueTypes(matchedSymbol.Parameters);

            if (matchedSymbol.Kind == MethodKind::CLASS_INSTANCE)
                method.ParamTypes.insert(method.ParamTypes.begin(), ValueType::POINTER);
        }

        type->VTable.push_back(std::move(method));
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
        ffiArgTypes[i] = Utils::GetFFIType(localValues[i].Type);
        ffiArgValues[i] = Utils::GetFFIArgPtr(localValues[i]);
    }

    ffi_cif cif;
    ffi_type* ffiRet = Utils::GetFFIType(method->ReturnType);

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ffiRet, ffiArgTypes.data()) != FFI_OK)
    {
        std::cerr << "[CallExternalMethod] ffi_prep_cfi failed\n";
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
        case ValueType::POINTER: return ret.p;
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

std::vector<Symbol> Capybara::ProcessDwarf(const std::filesystem::path& filePath)
{
    std::vector<Symbol> out;

    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "ERROR: failed to open: " << filePath.string() << std::endl;
        return {};
    }

    
    elf::elf ef(elf::create_mmap_loader(fd));
    dwarf::dwarf dw(dwarf::elf::create_loader(ef));

    Utils::TraverseAndCollect(dw, out);

    close(fd);
    return out;
}

void Capybara::RegisterMethod(CapyClass* type, const std::string& name, void (*fn)(CapyObject*))
{
    type->DeclaredMethods.push_back({name, fn});
}

MethodEntry Capybara::ConvertDeclaredToMethod(const DeclaredMethodEntry& d)
{
    return MethodEntry{d.Name, CPY_REINTERPRET_GEN(d.Fn) };
}

void Capybara::BuildVTable(CapyClass* type)
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

MethodEntry* Capybara::GetMethod(CapyClass* type, const std::string &methodName)
{
    if (!type) return nullptr;
    for (auto& method : type->VTable)
    {
        if (method.Name == methodName)
            return &method;
    }
    return nullptr;
}

