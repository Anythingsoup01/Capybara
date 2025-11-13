#include "Runtime.hpp"
#include "cpypch.h"
#include "Capybara.h"
#include "Utility.h"


#include <cstring>
#include <dlfcn.h>
#include <libelfin/elf/elf++.hh>
#include <elf.h>
#include <libelfin/dwarf/dwarf++.hh>

static Storage s_Storage;

static void remove_core_classes(CapyDomain* cd)
{
    std::vector<std::string> coreClasses;
    for (auto& [name, lib] : cd->Libraries)
    {
        if (!lib->IsCore)
            continue;

        CapyImage* image = lib->MainImage.get();
        for (auto& [name, klass] : image->Classes)
        {
            if (name.empty())
                continue;
            coreClasses.push_back(name);
        }
    }

    for (auto& [name, lib] : cd->Libraries)
    {
        if (lib->IsCore)
            continue;

        CapyImage* image = lib->MainImage.get();
        std::unordered_map<std::string, std::unique_ptr<CapyClass>> goodClasses;
        for (auto& [name, klass] : image->Classes)
        {
            bool found = false;
            for (auto& coreClass : coreClasses)
            {
                if (strncmp(name.c_str(), coreClass.c_str(), coreClass.length()) == 0)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                goodClasses[name] = std::move(klass);
            }
        }

        image->Classes = std::move(goodClasses);

    }
}

static void update_symbol_namespaces(std::vector<Symbol>& symbols)
{
    for (auto& sym : symbols)
    {
        std::string& NameSpace = sym.Namespace;
        //std::cout << "NAMESPACE: " << NameSpace << "::" << sym.Name << "\n";
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

static void traverse_and_collect(const dwarf::die& d, std::vector<std::string>& scope_stack, std::vector<Symbol>& outSymbols)
{
    std::string name = get_short_name(d);

    // Keep track of scopes
    bool is_namespace = d.tag == dwarf::DW_TAG::namespace_;
    bool is_classname = d.tag == dwarf::DW_TAG::class_type;
    bool is_structure = d.tag == dwarf::DW_TAG::structure_type;
    bool is_union = d.tag == dwarf::DW_TAG::union_type;

    bool is_scope = is_namespace || is_classname || is_structure || is_union;

    if (is_namespace && !name.empty())
    {

        for (auto& ignoredNamespace : s_Storage.IgnoredNamespaces)
        {
            if (strncmp(name.c_str(), ignoredNamespace.c_str(), ignoredNamespace.length()) == 0)
            {
                return;
            }
        }
    }
    if ((is_classname || is_structure) && !name.empty())
    {
        for (auto& ignoredClassName : s_Storage.IgnoredClassNames)
        {
            if (strncmp(name.c_str(), ignoredClassName.c_str(), ignoredClassName.length()) == 0)
            {
                return;
            }
        }
    }

    if (is_scope && !name.empty())
        scope_stack.push_back(name);




    // Process functions
    if (d.tag == dwarf::DW_TAG::subprogram) {

        if (scope_stack.empty() && s_Storage.IgnoreEmptyNamespaces)
            return;

        // This removes any mangled symbols, not sure why
        // there are any in -gdwarf-4 but there is :(
        if (strncmp(name.c_str(), "_ZN", 3) == 0)
            return;


        std::vector<std::string> full_scope(scope_stack);

        std::string qualified_name;
        for (size_t i = 0; i < full_scope.size(); ++i)
        {
            if (i > 0) qualified_name += "::";
            qualified_name += full_scope[i];
        }

        // Intentionally leaving the class name empty
        // If we get an instance of Class* this,
        // then we will convert it to the ClassName.
        Symbol sym;
        std::string name = get_short_name(d);
        if (strs_n_equal(name, {"<anon>"}))
            return;
        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = get_return_type(d);
        sym.IsVariable = false;
        sym.IsClassInstance = false;
        sym.Offset = 0;


        for (auto& child : d)
        {
            if (child.tag != dwarf::DW_TAG::formal_parameter)
                continue;

            if (!child.has(dwarf::DW_AT::type))
                continue;

            std::string paramType = resolve_type(child[dwarf::DW_AT::type].as_reference());
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

    if (d.tag == dwarf::DW_TAG::member)
    {
        if (scope_stack.empty() && s_Storage.IgnoreEmptyNamespaces)
            return;

        std::vector<std::string> full_scope(scope_stack);

        std::string qualified_name;
        for (size_t i = 0; i < full_scope.size(); ++i) {
            if (i > 0) qualified_name += "::";
            qualified_name += full_scope[i];
        }

        uint64_t offset = 0;

        if (d.has(dwarf::DW_AT::data_member_location)) 
        {
            const auto& attr = d[dwarf::DW_AT::data_member_location];

            switch (attr.get_form()) 
            {
                case dwarf::DW_FORM::data1:
                case dwarf::DW_FORM::data2:
                case dwarf::DW_FORM::data4:
                case dwarf::DW_FORM::data8:
                case dwarf::DW_FORM::udata:
                case dwarf::DW_FORM::sdata:
                    offset = attr.as_uconstant();
                    break;

                case dwarf::DW_FORM::exprloc:
                    std::cout << "VARIABLE NEEDS TO BE EVALUATED!\n";
                    break;
                default:
                    break;
            }

        }

        // Intentionally leaving the class name empty
        // If we get an instance of Class* this,
        // then we will convert it to the ClassName.
        Symbol sym;
        std::string name = get_short_name(d);
        if (strs_n_equal(name, {"<anon>"}))
            return;

        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = get_return_type(d);
        sym.IsVariable = true;
        sym.IsClassInstance = true;
        sym.Offset = offset;

        outSymbols.push_back(sym);

    }

    if (d.tag == dwarf::DW_TAG::variable)
    {
        if (scope_stack.empty() && s_Storage.IgnoreEmptyNamespaces)
            return;
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
        std::string name = get_short_name(d);
        if (strs_n_equal(name, {"<anon>"}))
            return;
        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = get_return_type(d);
        sym.IsVariable = true;
        sym.IsClassInstance = false;
        sym.Offset = 0;

        outSymbols.push_back(sym);

    }

    for (auto &child : d)
        traverse_and_collect(child, scope_stack, outSymbols);

    if (is_scope && !name.empty())
        scope_stack.pop_back();
}

static std::vector<Symbol> process_library(const elf::elf& ef, const std::vector<Symbol>& symbols)
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

            std::string demangledName = demangle_symbol_name(sym.get_name().c_str());

            size_t paren = demangledName.find("(");
            if (paren != std::string::npos)
                demangledName = demangledName.substr(0, paren);

            symbolNames[demangledName] = sym.get_name().c_str();
        }
    }


    std::vector<Symbol> tmp;
    
    for (auto& sym : symbols)
    {
        std::string fullName;
        if (!sym.Namespace.empty())
            fullName += sym.Namespace + "::";
        if (!sym.ClassName.empty())
            fullName += sym.ClassName + "::"; 

        fullName += sym.Name;

        auto it = symbolNames.find(fullName);
        if (it != symbolNames.end())
        {
            Symbol resolved = sym;
            resolved.Signature = it->second;
            tmp.push_back(resolved);
        }
    }



    for (auto sym : symbols)
    {
        bool ignored = false;
        for (auto& ignoredClassName : s_Storage.IgnoredClassNames)
        {
            if (strncmp(sym.Name.c_str(), ignoredClassName.c_str(), ignoredClassName.length()) == 0)
            {
                ignored = true;
                break;
            }
        }

        if (ignored)
            continue;

        std::vector<std::string> ignoredNames = { "gp_offset", "fp_offset", "overflow_arg_area", "reg_save_area", "tm_", "_vptr." };

        for (auto& ignoredName : ignoredNames)
        {
            if (strncmp(sym.Name.c_str(), ignoredName.c_str(), ignoredName.length()) == 0)
            {
                ignored = true;
                break;
            }

        }

        if (ignored)
            continue;

        if (sym.Offset >= 0 && (sym.IsClassInstance && sym.IsVariable))
        {
            tmp.push_back(sym);
            continue;
        }
    }

    return tmp;
}

void capy_init()
{
    s_Storage = Storage();
    s_Storage.IgnoredNamespaces = { "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_" };
    s_Storage.IgnoredClassNames = { "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_", "__pthread", "timespec", "lconv", "_M_" };
    s_Storage.IgnoreEmptyNamespaces = false;
}

void capy_shutdown()
{
    for (auto& [name, domain] : s_Storage.Domains)
    {
        CapyDomain* d = domain.get();
        capy_unload_domain(name);
    }
}

void capy_set_libraries_path(const std::filesystem::path &libPath)
{
    s_Storage.SearchPath = libPath;
}

void capy_set_ignored_namespace(const std::vector<std::string>& ignoredNamespace)
{
    for (auto& nameSpace : ignoredNamespace)
        s_Storage.IgnoredNamespaces.push_back(nameSpace);
}

void capy_set_ignore_empty_namespace(bool active)
{
    s_Storage.IgnoreEmptyNamespaces = active;
}

void capy_set_ignored_classname(const std::vector<std::string>& ignoredClassName)
{
    for (auto& className : ignoredClassName)
        s_Storage.IgnoredClassNames.push_back(className);
}

CapyDomain* capy_init_domain(const std::string& name)
{
    if (s_Storage.Domains.find(name) != s_Storage.Domains.end())
    {
        std::cerr << "ERROR: domain '" << name << "' already exists!\n";
        return nullptr;
    }

    auto domain = std::make_unique<CapyDomain>();
    CapyDomain* ptr = domain.get();
    s_Storage.Domains[name] = std::move(domain);
    return ptr;
}

void capy_unload_domain(const std::string& domainName)
{
    auto it = s_Storage.Domains.find(domainName);
    if (it == s_Storage.Domains.end())
    {
        std::cout << "Domain: " << domainName << " doesn't exist!\n";
        return;
    }

    s_Storage.Domains.erase(it);
    s_Storage.InternalCalls.clear();
}

std::string capy_dump_domain(const std::string& domainName)
{
    if (s_Storage.Domains.find(domainName) == s_Storage.Domains.end())
    {
        std::cerr << "capy_dump_domain: Domain '" << domainName << "' doesn't exist!\n";
        return "<null>\n";
    }
    std::string str = "Domain: " + domainName + "\n";
    CapyDomain* domain = s_Storage.Domains[domainName].get();
    for (auto& [libName, lib] : domain->Libraries)
    {
        str.append("  Library: " + libName + "\n");
        CapyLibrary* libPtr = lib.get();
        CapyImage* imagePtr = libPtr->MainImage.get();
        for (auto& [className, klass] : imagePtr->Classes)
        {
            std::string adjustedClassName = className.empty() ? "<Functions Only>" : className;
            str.append("    FullClassName: " + adjustedClassName + "\n");
            CapyClass* klassPtr = klass.get();
            for (auto& [name, sym] : klassPtr->Symbols)
            {
                str.append("      Symbol: " + sym.Signature + "\n");
                std::string symType = sym.IsVariable ? "Variable" : "Method";
                std::string adjustedSymName;
                if (!sym.Namespace.empty())
                    adjustedSymName.append(sym.Namespace + "::");
                if (!sym.ClassName.empty())
                    adjustedSymName.append(sym.ClassName + "::");

                adjustedSymName.append(sym.Name);
                str.append("        (" + symType + ") " + sym.ReturnType + " " + adjustedSymName);

                if (!sym.IsVariable)
                {
                    str.append("(");
                    bool first = true;
                    for (auto& param : sym.ParameterTypes)
                    {
                        if (!first) str.append(", ");
                        str.append(param);
                        first = false;
                    }
                    str.append(")");
                }
                str.append(";\n");
            }
        }
    }

    return str;

}

void capy_reload_libraries_into_domain(CapyDomain* cd)
{
    for (auto& entry : std::filesystem::directory_iterator(s_Storage.SearchPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".so")
        {
            capy_domain_library_open(cd, entry.path().filename(), false);
        }
    }
}

CapyLibrary* capy_domain_library_open(CapyDomain* d, const std::string& libName, bool isCore)
{
    std::filesystem::path libPath = s_Storage.SearchPath;
    libPath.append(libName);

    int fd = open(libPath.c_str(), O_RDONLY);
    if (fd < 0 && !s_Storage.SearchPath.empty())
    {
        fd = open(libName.c_str(), O_RDONLY);
        if (fd < 0)
        {
            std::cerr << "ERROR: failed to open file: " << libPath.generic_string() << "\n";
            return nullptr;
        }
        libPath = std::filesystem::path(libName);
    }



    if (d->Libraries.find(libPath.filename().string()) != d->Libraries.end())
    {
        return d->Libraries.at(libPath.filename().string()).get();
    }

    elf::elf ef(elf::create_mmap_loader(fd));
    dwarf::dwarf dw;
    try {
        dw = dwarf::dwarf(dwarf::elf::create_loader(ef));
    } catch (std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
    }
    auto image = std::make_unique<CapyImage>();
    std::vector<Symbol> symbols;

    for (auto &cu : dw.compilation_units())
        traverse_and_collect(cu.root(), *(new std::vector<std::string>), symbols);

    close(fd);

    update_symbol_namespaces(symbols);

    symbols = process_library(ef, symbols);

    void* instance = dlopen(libPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!instance)
    {
        std::cerr << "ERROR: " << dlerror() << "\n";
        return nullptr;
    }

    std::unordered_map<std::string, std::unique_ptr<CapyClass>> classes;

    for (auto& sym : symbols)
    {
        void* handle = nullptr;
        if (!sym.Signature.empty())
            handle = dlsym(instance, sym.Signature.c_str());
        

        // Try internal calls first (highest priority)
        if (s_Storage.InternalCalls.contains(sym.Name))
        {
            handle = s_Storage.InternalCalls[sym.Name];
        }

        if (!handle && (!sym.IsVariable && !sym.IsClassInstance))
            continue;

        std::string fullName;
        if (!sym.Namespace.empty())
            fullName += sym.Namespace;
        if (!sym.ClassName.empty())
            fullName += sym.ClassName;


        // Ensure class exists or create new
        CapyClass* klass = nullptr;
        auto it = classes.find(fullName);
        if (it == classes.end())
        {
            auto newKlass = std::make_unique<CapyClass>();
            newKlass->NameSpace = sym.Namespace;
            newKlass->ClassName = sym.ClassName;
            newKlass->VTable = std::make_unique<CapyVTable>();
            klass = newKlass.get();
            classes[fullName] = std::move(newKlass);
        }
        else
        {
            klass = it->second.get();
        }

        // Store symbol metadata
        klass->Symbols[sym.Name] = sym;

        if (sym.IsVariable)
        {
            auto field = std::make_unique<CapyField>();
            field->SymHandle = handle;
            field->FieldType = string_to_value_type(sym.ReturnType);
            field->FieldTypeString = sym.ReturnType;
            field->Offset = sym.Offset;
            field->ClassMember = sym.IsVariable && sym.IsClassInstance;
            klass->VTable->Fields[sym.Name] = std::move(field);
        }
        else
        {
            auto method = std::make_unique<CapyMethod>();
            method->SymHandle = handle;
            method->ReturnType = string_to_value_type(sym.ReturnType);

            if (sym.IsClassInstance)
            {
                method->ClassMember = true;
                method->Parameters.push_back(ValueType::POINTER);
            }
            for (auto& param : sym.ParameterTypes)
            {
                ValueType type = string_to_value_type(param);
                if (type != ValueType::VOID)
                    method->Parameters.push_back(type);
            }

            klass->VTable->Methods[sym.Name] = std::move(method);
        }
    }
    image->Classes = std::move(classes);

    std::unique_ptr<CapyLibrary> library = std::make_unique<CapyLibrary>(std::move(image));
    library->SymbolInstance = std::move(instance);
    library->IsCore = isCore;

    if (isCore)
        d->CoreLibraries.push_back(libPath.filename().c_str());


    d->Libraries[libPath.filename().c_str()] = std::move(library);

    return d->Libraries.at(libPath.filename().string()).get();
}

std::vector<std::string> capy_get_core_libraries_from_domain(const std::string &domainName)
{
    auto it = s_Storage.Domains.find(domainName);
    if (it == s_Storage.Domains.end())
    {
        std::cerr << "ERROR: domain '" << domainName << "' doesn't exists!\n";
        return {};
    }

    CapyDomain* domain = it->second.get();

    return domain->CoreLibraries;
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

void capy_field_data_get_from_class(void* instance, CapyClass* cc, const std::string& fieldName, void* value)
{
    CapyField* f = capy_field_from_class(cc, fieldName);
    if (instance)
    {
        void* offsetVoidPTR = static_cast<void*>(static_cast<char*>(instance) + f->Offset);
        memcpy(value, offsetVoidPTR, type_size(f->FieldType));
    }
    else
    {
        memcpy(value, f->SymHandle, type_size(f->FieldType));
    }
}

void capy_field_data_get_from_field(void* instance, CapyField* cf, void* value)
{
    if (instance)
    {
        void* offsetVoidPTR = static_cast<void*>(static_cast<char*>(instance) + cf->Offset);
        memcpy(value, offsetVoidPTR, type_size(cf->FieldType));
    }
    else
    {
        memcpy(value, cf->SymHandle, type_size(cf->FieldType));
    }
}

void capy_field_data_set_from_class(void* instance, CapyClass* cc, const std::string& fieldName, void* value)
{
    CapyField* f = capy_field_from_class(cc, fieldName);
    if (instance)
    {
        void* offsetVoidPTR = static_cast<void*>(static_cast<char*>(instance) + f->Offset);
        memcpy(offsetVoidPTR, value, type_size(f->FieldType));
    }
    else
    {
        memcpy(f->SymHandle, value, type_size(f->FieldType));
    }
}

void capy_field_data_set_from_field(void* instance, CapyField* cf, void* value)
{
    if (instance)
    {
        void* offsetVoidPTR = static_cast<void*>(static_cast<char*>(instance) + cf->Offset);
        memcpy(offsetVoidPTR, value, type_size(cf->FieldType));
    }
    else
    {
        memcpy(cf->SymHandle, value, type_size(cf->FieldType));
    }
}

void* capy_function_call_from_method(CapyMethod* method, const std::vector<RuntimeValue>& values)
{
    if (!method || !method->SymHandle) return nullptr;

    if (values.size() != method->Parameters.size())
        throw std::runtime_error("Incorrect number of arguments provided!");

    size_t nargs = values.size();
    std::vector<ffi_type*> ffiArgTypes(nargs);
    std::vector<void*> ffiArgValues(nargs);
    std::vector<RuntimeValue> localValues = values;

    for (size_t i = 0; i < method->Parameters.size(); ++i) {
        ffiArgTypes[i] = get_ffi_type_p(method->Parameters[i]);
        ffiArgValues[i] = get_ffi_arg_p(localValues[i]);
    }

    ffi_cif cif;
    ffi_type* ffiRet = get_ffi_type_p(method->ReturnType);


    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ffiRet, ffiArgTypes.data()) != FFI_OK)
    {
        std::cerr << "ffi_prep_cif failed\n";
        return nullptr;
    }

    union {
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint16_t ui16;
        uint32_t ui32;
        uint64_t ui64;
        float f;
        void* p;
    } ret;

    // For member functions, reinterpret the pointer as a callable (void*, ...)
    if (method->ClassMember)
    {
        using MemberFn = void(*)(...);
        ffi_call(&cif, FFI_FN(reinterpret_cast<MemberFn>(method->SymHandle)), &ret, ffiArgValues.data());
    }
    else
    {
        ffi_call(&cif, FFI_FN(method->SymHandle), &ret, ffiArgValues.data());
    }

    switch(method->ReturnType)
    {
        case ValueType::INT16: return new int16_t(ret.i16);
        case ValueType::INT32: return new int32_t(ret.i32);
        case ValueType::INT64: return new int64_t(ret.i64);
        case ValueType::UINT16: return new uint16_t(ret.ui16);
        case ValueType::UINT32: return new uint32_t(ret.ui32);
        case ValueType::UINT64: return new uint64_t(ret.ui64);
        case ValueType::FLOAT: return new float(ret.f);
        case ValueType::POINTER: return ret.p;
        case ValueType::VOID: return nullptr;
    }

    return nullptr;}

template<> constexpr const char* capy_type_name<float>() { return "Float"; }
template<> constexpr const char* capy_type_name<double>() { return "Double"; }
template<> constexpr const char* capy_type_name<int16_t>() { return "Int16"; }
template<> constexpr const char* capy_type_name<int32_t>() { return "Int32"; }
template<> constexpr const char* capy_type_name<int64_t>() { return "Int64"; }
template<> constexpr const char* capy_type_name<uint16_t>() { return "UInt16"; }
template<> constexpr const char* capy_type_name<uint32_t>() { return "UInt32"; }
template<> constexpr const char* capy_type_name<uint64_t>() { return "UInt64"; }



void capy_add_internal_call(const std::string& name, void* functionSymbol)
{
    if (s_Storage.InternalCalls.contains(name)) return;

    s_Storage.InternalCalls[name] = functionSymbol;

    
    for (auto& [_, domain] : s_Storage.Domains)
    {
        for (auto& [_, library] : domain->Libraries)
        {
            if (!library->SymbolInstance) continue;

            CapyImage* img = library->MainImage.get();
            for (auto& [_, cls] : img->Classes)
            {
                for (auto& [symName, sym] : cls->Symbols)
                {
                    if (symName == name)
                    {
                        // Directly patch the pointer in the plugin
                        void** addr = reinterpret_cast<void**>(cls->VTable->Fields[symName]->SymHandle);
                        if (addr)
                            *addr = functionSymbol;
                    }
                }
            }
        }
    }
}
