#include "Capybara.h"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cxxabi.h>
#include <ffi.h>
#include <dlfcn.h>

#include <libelfin/elf/elf++.hh>
#include <libelfin/dwarf/dwarf++.hh>

template<>
int RuntimeValue::As<int>() const
{
    if (Type != ValueType::INT32) throw std::runtime_error("Type mismatch for int");
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


static Storage s_Storage;

static std::string demangle(const char* name)
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

static bool StrsNEqual(const std::string& mainString, const std::vector<std::string>& comparedTo)
{
    bool isEqual = false;



    for (auto& check : comparedTo)
    {
        if (strncmp(mainString.c_str(), check.c_str(), check.length()) == 0)
        {
            isEqual = true;
            break;
        }
    }
    return isEqual;
}

// This one checks the length
static bool StrNEqual(const std::string& mainString, const std::string& comparedTo)
{
    if (mainString.length() != comparedTo.length())
        return false;

    if (strncmp(mainString.c_str(), comparedTo.c_str(), mainString.length()) == 0)
    {
        return true;
    }

    return false;
}

static ValueType StringToValueType(const std::string& value)
{
    // Should probably return a null enum or assert
    if (value.empty())
        return ValueType::VOID;

    if (value.find("*") != std::string::npos)
        return ValueType::POINTER;


    if (StrsNEqual(value, { "void", "void " }))
        return ValueType::VOID;

    if (StrsNEqual(value, { "const std::string", "const std::string& ", "const std::string", "std::string", "std::string " }))
        return ValueType::POINTER;

    if (StrsNEqual(value, { "int", "int ", "int32_t", "int32_t " }))
        return ValueType::INT32;

    if (StrsNEqual(value, { "float", "float " }))
        return ValueType::FLOAT;

    return ValueType::VOID;
}

ffi_type* GetFFIType(ValueType type)
{
    switch (type)
    {
        case ValueType::INT32: return &ffi_type_sint32;
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
        case ValueType::INT32: return &val.i;
        case ValueType::FLOAT: return &val.f;
        case ValueType::POINTER: return &val.p;
        default: return nullptr;
    }
}

size_t type_size(ValueType type)
{
    switch (type)
    {
        case ValueType::FLOAT: return sizeof(float);
        case ValueType::INT32: return sizeof(int32_t);
        case ValueType::POINTER: return sizeof(void*);
        case ValueType::VOID: return 0;
    }

    return 0;
}

// Get the name of a type DIE
static std::string GetShortName(const dwarf::die& die)
{
    // fallback: mangled name
    if (die.has(dwarf::DW_AT::name))
        return die[dwarf::DW_AT::name].as_string();
    // fallback: mangled name
    if (die.has(dwarf::DW_AT::linkage_name))
        return die[dwarf::DW_AT::linkage_name].as_string();

    return "<anon>";
}

static std::string ResolveType(const dwarf::die& type_die)
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

static std::string GetTypeName(const dwarf::die& typeDie)
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

static std::string GetReturnType(const dwarf::die& die)
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


static void UpdateSymbolNamespaces(std::vector<Symbol>& symbols)
{
    for (auto& sym : symbols)
    {
        std::string& NameSpace = sym.Namespace;
        for (auto& knownName : s_Storage.KnownClassNames)
        {
            size_t foundName = NameSpace.rfind(knownName);
            if (foundName != std::string::npos)
            {
                std::string className = NameSpace.substr(foundName);
                if (className.length() != knownName.length()) continue;
                if (foundName - 2 != std::string::npos)
                    NameSpace = NameSpace.substr(0, foundName - strlen("::"));
                else
                    NameSpace.clear();

                sym.ClassName = knownName;
                break;
            }
        }
    }
}

// Recursively walk DIEs and print functions
static void TraverseAndCollect(const dwarf::die& d, std::vector<std::string>& scope_stack, std::vector<Symbol>& outSymbols)
{
    std::string name = GetShortName(d);

    // Keep track of scopes
    bool is_scope = (d.tag == dwarf::DW_TAG::namespace_ ||
                     d.tag == dwarf::DW_TAG::class_type ||
                     d.tag == dwarf::DW_TAG::structure_type ||
                     d.tag == dwarf::DW_TAG::union_type);

    if (is_scope && !name.empty())
    {
        if (StrsNEqual(name, { "std", "__gnu_", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_" }))
            return;

        scope_stack.push_back(name);

    }

    // Process functions
    if (d.tag == dwarf::DW_TAG::subprogram) {
        std::vector<std::string> full_scope(scope_stack);

        std::string qualified_name;
        for (size_t i = 0; i < full_scope.size(); ++i) {
            if (i > 0) qualified_name += "::";
            qualified_name += full_scope[i];
        }

        // Intentionally leaving the class name empty
        // If we get an instance of Class* this,
        // then we will convert it to the ClassName.
        Symbol sym;
        std::string name = GetShortName(d);
        if (StrsNEqual(name, {"<anon>"}))
            return;
        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = GetReturnType(d);
        sym.IsVariable = false;
        sym.IsClassInstance = false;


        for (auto& child : d)
        {
            if (child.tag != dwarf::DW_TAG::formal_parameter)
                continue;

            std::string paramType = ResolveType(child[dwarf::DW_AT::type].as_reference());
            size_t pointer = paramType.rfind("*");
            if (pointer != std::string::npos)
            {
                std::string lastNamespace;
                std::string typeName = paramType.substr(0, pointer);
                size_t lastNamespacePos = qualified_name.rfind("::");

                if (lastNamespacePos != std::string::npos)
                    lastNamespace = qualified_name.substr(lastNamespacePos + 2);
                else
                    lastNamespace = qualified_name;

                if (strncmp(lastNamespace.c_str(), typeName.c_str(), typeName.size()) == 0)
                {
                    for (auto& name : s_Storage.KnownClassNames)
                    {
                        if (name == typeName)
                        {
                            sym.IsClassInstance = true;
                            continue;
                        }
                    }
                    s_Storage.KnownClassNames.push_back(typeName);
                    sym.IsClassInstance = true;
                }
                else
                {
                    sym.ParameterTypes.push_back(paramType);
                }

            }
            else
            {
                sym.ParameterTypes.push_back(paramType);
            }
        }


        outSymbols.push_back(sym);

    }

    if (d.tag == dwarf::DW_TAG::variable)
    {
        std::vector<std::string> full_scope(scope_stack);

        std::string qualified_name;
        for (size_t i = 0; i < full_scope.size(); ++i) {
            if (i > 0) qualified_name += "::";
            qualified_name += full_scope[i];
        }

        // Intentionally leaving the class name empty
        // If we get an instance of Class* this,
        // then we will convert it to the ClassName.
        Symbol sym;
        std::string name = GetShortName(d);
        if (StrsNEqual(name, {"<anon>"}))
            return;
        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = GetReturnType(d);
        sym.IsVariable = true;

        outSymbols.push_back(sym);

    }

    for (auto &child : d)
        TraverseAndCollect(child, scope_stack, outSymbols);

    if (is_scope && !name.empty())
        scope_stack.pop_back();
}

static std::vector<Symbol> ProcessLibrary(const elf::elf& ef, const std::vector<Symbol>& symbols)
{
    std::unordered_map<std::string, std::string> symbolNames;
    for (auto &sec : ef.sections())
    {
        if (sec.get_hdr().type != elf::sht::dynsym)
            continue;

        for (auto sym : sec.as_symtab())
        {
            auto &d = sym.get_data();
            if (d.shnxd == elf::shn::undef) continue;

            std::string demangledName = demangle(sym.get_name().c_str());

            size_t paren = demangledName.find("(");
            if (paren != std::string::npos)
                demangledName = demangledName.substr(0, paren);

            symbolNames[demangledName] = sym.get_name().c_str();

        }
    }

    std::vector<Symbol> tmp;
    
    for (auto& [demangled, mangled] : symbolNames)
    {
        for (auto& sym : symbols)
        {
            std::string fullName;
            if (!sym.Namespace.empty())
                fullName += sym.Namespace + "::";
            if (!sym.ClassName.empty())
                fullName += sym.ClassName + "::"; 

            fullName += sym.Name;

            if (fullName.length() != demangled.length() && fullName.length() != demangled.length() - 2)
            {
                continue;
            }

            
            if (strncmp(fullName.c_str(), demangled.c_str(), fullName.length()) == 0)
            {
                Symbol adjustedSym = sym;
                adjustedSym.Signature = mangled;
                tmp.push_back(adjustedSym);
                break;
            }

        }
    }

    return tmp;
}

void capy_init()
{
    s_Storage = Storage();
}

void capy_shutdown()
{
    if (!s_Storage.SymbolInstances.empty())
    {
        for (auto* instance : s_Storage.SymbolInstances)
        {
            dlclose(instance);
        }
    }
}



CapyDomain* capy_init_domain(const std::string& name)
{
    if (s_Storage.Domains.find(name) != s_Storage.Domains.end())
    {
        std::cerr << "ERROR: domain '" << name << "' already exists!\n";
        return nullptr;
    }

    auto* d = s_Storage.Domains[name].get();
    d = std::move(new CapyDomain);

    return d;
}

CapyLibrary* capy_domain_library_open(CapyDomain* d, const std::filesystem::path& libPath)
{

int fd = open(libPath.c_str(), O_RDONLY);
    if (!fd)
    {
        std::cerr << "ERROR: failed to open file: " << libPath.generic_string() << "\n";
        return nullptr;
    }

    if (d->Libraries.find(libPath.c_str()) != d->Libraries.end())
    {
        std::cerr << "ERROR: library '" << libPath.c_str() << "' already exists!\n";
        return nullptr;
    }

    elf::elf ef(elf::create_mmap_loader(fd));
    dwarf::dwarf dw(dwarf::elf::create_loader(ef));

    CapyImage* image = new CapyImage;
    std::vector<Symbol> symbols;

    for (auto &cu : dw.compilation_units())
        TraverseAndCollect(cu.root(), *(new std::vector<std::string>), symbols);

    close(fd);

    UpdateSymbolNamespaces(symbols);

    symbols = ProcessLibrary(ef, symbols);

    void* instance = dlmopen(LM_ID_NEWLM, libPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!instance)
    {
        std::cerr << "ERROR: Failed to open file: " << libPath.generic_string() << "\n";
        return nullptr;
    }

    s_Storage.SymbolInstances.push_back(instance);

    std::unordered_map<std::string, std::unique_ptr<CapyClass>> classes;

    for (auto& sym : symbols)
    {

        void* handle = dlsym(instance, sym.Signature.c_str());
        if (!handle)
        {
            std::cerr << "ERROR: symbol " << sym.Signature << " failed\n";
            continue;
        }

        std::string fullName;
        if (!sym.Namespace.empty())
            fullName += sym.Namespace;
        if (!sym.ClassName.empty())
            fullName += sym.ClassName;

        bool found = false;

        for (auto& [fullNameSpace, Klass] : classes)
        {
            if (StrNEqual(fullNameSpace, fullName))
            {
                Klass->Symbols[sym.Name] = sym;
                if (sym.IsVariable)
                {
                    std::cout << sym.Name << "\n";
                    CapyField* fld = new CapyField;
                    fld->SymHandle = handle;
                    fld->FieldType = StringToValueType(sym.ReturnType);
                    Klass->VTable->Fields.emplace(std::pair<std::string, std::unique_ptr<CapyField>>(sym.Name, std::move(fld)));
                }
                else
                {
                    CapyMethod* method = new CapyMethod;
                    method->SymHandle = handle;
                    method->ReturnType = StringToValueType(sym.ReturnType);
                    if (sym.IsClassInstance)
                        method->Parameters.push_back(ValueType::POINTER);

                    for (auto& param : sym.ParameterTypes)
                    {
                        ValueType type = StringToValueType(param);
                        if (type != ValueType::VOID)
                            method->Parameters.push_back(type);
                    }
                    Klass->VTable->Methods.emplace(std::pair<std::string, std::unique_ptr<CapyMethod>>(sym.Name, std::move(method)));
                }
                found = true;
            }
        }

        if (!found)
        {
            classes.emplace(std::pair<std::string, std::unique_ptr<CapyClass>>(fullName, std::move(new CapyClass)));
            auto& Klass = classes[fullName];
            Klass->VTable.reset(new CapyVTable);
            if (sym.IsVariable)
            {
                CapyField* fld = new CapyField;
                fld->SymHandle = handle;
                fld->FieldType = StringToValueType(sym.ReturnType);
                Klass->VTable->Fields.emplace(std::pair<std::string, std::unique_ptr<CapyField>>(sym.Name, std::move(fld)));
            }
            else
            {
                CapyMethod* method = new CapyMethod;
                method->SymHandle = handle;
                method->ReturnType = StringToValueType(sym.ReturnType);
                for (auto& param : sym.ParameterTypes)
                {
                    ValueType type = StringToValueType(param);
                    if (type != ValueType::VOID)
                        method->Parameters.push_back(type);
                }
                Klass->VTable->Methods.emplace(std::pair<std::string, std::unique_ptr<CapyMethod>>(sym.Name, std::move(method)));
            }

        }
    }

    image->Classes = std::move(classes);

    CapyLibrary* library = new CapyLibrary(image);

    d->Libraries.emplace(std::pair<std::string, std::unique_ptr<CapyLibrary>>(libPath.generic_string(), std::move(library)));

    return d->Libraries.at(libPath.generic_string()).get();


}

CapyImage* capy_library_get_image(CapyLibrary* l)
{
    return l->MainImage.get();
}

CapyClass* capy_class_from_name(CapyImage* i, const std::string& nameSpace, const std::string& className)
{
    std::string fullName;
    if (!nameSpace.empty())
        fullName += nameSpace;
    if (!className.empty())
        fullName += className;

    if (i->Classes.find(fullName) != i->Classes.end())
        return i->Classes.at(fullName).get();

    return nullptr;
}

CapyMethod* capy_method_from_class(CapyClass* c, const std::string& functionName)
{
    return c->VTable->Methods[functionName].get();
}

CapyField* capy_field_from_class(CapyClass* c, const std::string& fieldName)
{
    return c->VTable->Fields[fieldName].get();
}

void capy_field_data_get_from_class(CapyClass* cc, const std::string& fieldName, void* value)
{
    CapyField* f = capy_field_from_class(cc, fieldName);
    memcpy(value, f->SymHandle, type_size(f->FieldType));
}

void capy_field_data_get_from_field(CapyField* cf, const std::string& fieldName, void* value)
{
    memcpy(value, cf->SymHandle, type_size(cf->FieldType));
}

void capy_field_data_set_from_class(CapyClass* cf, const std::string& fieldName, void* value)
{
    CapyField* f = capy_field_from_class(cf, fieldName);
    memcpy(f->SymHandle, value, type_size(f->FieldType));
}

void capy_field_data_set_from_field(CapyField* cf, const std::string& fieldName, void* value)
{
    memcpy(cf->SymHandle, value, type_size(cf->FieldType));
}

void* capy_function_call_from_method(CapyMethod* method, const std::vector<RuntimeValue>& values)
{
    if (!method || !method->SymHandle) return nullptr;
    if (method->Parameters.size() != values.size())
        throw std::runtime_error("Too many arguments provided!");

    size_t nargs = values.size();

    std::vector<ffi_type*> ffiArgTypes(nargs);
    std::vector<void*> ffiArgValues(nargs);
    std::vector<RuntimeValue> localValues = values;

    for (size_t i = 0; i < nargs; ++i)
    {
        ffiArgTypes[i] = GetFFIType(localValues[i].Type);
        ffiArgValues[i] = GetFFIArgPtr(localValues[i]);
    }

    ffi_cif cif;
    ffi_type* ffiRet = GetFFIType(method->ReturnType);

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

    ffi_call(&cif, FFI_FN(method->SymHandle), &ret, ffiArgValues.data());
    switch(method->ReturnType)
    {
        case ValueType::INT32: return new int(ret.i);
        case ValueType::FLOAT: return new float(ret.f);
        case ValueType::POINTER: return ret.p;
        case ValueType::VOID: return nullptr;
    }
    return nullptr;
}

