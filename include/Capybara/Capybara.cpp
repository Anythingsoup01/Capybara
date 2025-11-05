#include "Runtime.hpp"
#include "cpypch.h"
#include "Capybara.h"
#include "Utility.h"

#include <libelfin/elf/elf++.hh>
#include <libelfin/dwarf/dwarf++.hh>
#include <memory>

static Storage s_Storage;

static void update_symbol_namespaces(std::vector<Symbol>& symbols)
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

static void traverse_and_collect(const dwarf::die& d, std::vector<std::string>& scope_stack, std::vector<Symbol>& outSymbols)
{
    std::string name = get_short_name(d);

    // Keep track of scopes
    bool is_scope = (d.tag == dwarf::DW_TAG::namespace_ ||
                     d.tag == dwarf::DW_TAG::class_type ||
                     d.tag == dwarf::DW_TAG::structure_type ||
                     d.tag == dwarf::DW_TAG::union_type);

    if (is_scope && !name.empty())
    {
        if (strs_n_equal(name, { "std", "__gnu_", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_" }))
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
        std::string name = get_short_name(d);
        if (strs_n_equal(name, {"<anon>"}))
            return;
        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = get_return_type(d);
        sym.IsVariable = false;
        sym.IsClassInstance = false;


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
        std::string name = get_short_name(d);
        if (strs_n_equal(name, {"<anon>"}))
            return;
        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = get_return_type(d);
        sym.IsVariable = true;

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
    if (it != s_Storage.Domains.end())
        s_Storage.Domains.erase(it);
}

void capy_reload_libraries_into_domain(CapyDomain* cd)
{
    for (auto& entry : std::filesystem::directory_iterator(s_Storage.SearchPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".so")
        {
            capy_domain_library_open(cd, entry.path().filename());
        }
    }
}

CapyLibrary* capy_domain_library_open(CapyDomain* d, const std::string& libName)
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
    dwarf::dwarf dw(dwarf::elf::create_loader(ef));

    CapyImage* image = new CapyImage;
    std::vector<Symbol> symbols;

    for (auto &cu : dw.compilation_units())
        traverse_and_collect(cu.root(), *(new std::vector<std::string>), symbols);

    close(fd);

    update_symbol_namespaces(symbols);

    symbols = process_library(ef, symbols);

    void* instance = dlmopen(LM_ID_NEWLM, libPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!instance)
    {
        std::cerr << "ERROR: Failed to open file: " << libPath.generic_string() << "\n";
        return nullptr;
    }



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
            if (str_n_equal(fullNameSpace, fullName))
            {
                Klass->Symbols[sym.Name] = sym;
                if (sym.IsVariable)
                {
                    std::unique_ptr<CapyField> field = std::make_unique<CapyField>();
                    field->SymHandle = handle;
                    field->FieldType = string_to_value_type(sym.ReturnType);
                    Klass->VTable->Fields[sym.Name] = std::move(field);
                }
                else
                {
                    std::unique_ptr<CapyMethod> method = std::make_unique<CapyMethod>();
                    method->SymHandle = handle;
                    method->ReturnType = string_to_value_type(sym.ReturnType);
                    if (sym.IsClassInstance)
                        method->Parameters.push_back(ValueType::POINTER);

                    for (auto& param : sym.ParameterTypes)
                    {
                        ValueType type = string_to_value_type(param);
                        if (type != ValueType::VOID)
                            method->Parameters.push_back(type);
                    }
                    Klass->VTable->Methods[sym.Name] = std::move(method);
                }
                found = true;
            }
        }

        if (!found)
        {
            std::unique_ptr<CapyClass> klass = std::make_unique<CapyClass>();
            classes[fullName] = std::move(klass);
            std::unique_ptr<CapyVTable> vtable = std::make_unique<CapyVTable>();
            classes[fullName]->VTable = std::move(vtable);
            if (sym.IsVariable)
            {
                std::unique_ptr<CapyField> field = std::make_unique<CapyField>();
                field->SymHandle = handle;
                field->FieldType = string_to_value_type(sym.ReturnType);
                classes[fullName]->VTable->Fields[sym.Name] = std::move(field);
            }
            else
            {
                std::unique_ptr<CapyMethod> method = std::make_unique<CapyMethod>();
                method->SymHandle = handle;
                method->ReturnType = string_to_value_type(sym.ReturnType);
                if (sym.IsClassInstance)
                    method->Parameters.push_back(ValueType::POINTER);

                for (auto& param : sym.ParameterTypes)
                {
                    ValueType type = string_to_value_type(param);
                    if (type != ValueType::VOID)
                        method->Parameters.push_back(type);
                }
                classes[fullName]->VTable->Methods[sym.Name] = std::move(method);
            }

        }
    }

    image->Classes = std::move(classes);

    std::unique_ptr<CapyLibrary> library = std::make_unique<CapyLibrary>(image);
    library->SymbolInstance = std::move(instance);

    d->Libraries[libPath.filename().c_str()] = std::move(library);

    return d->Libraries.at(libPath.filename().string()).get();


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

void capy_field_data_get_from_field(CapyField* cf, void* value)
{
    memcpy(value, cf->SymHandle, type_size(cf->FieldType));
}

void capy_field_data_set_from_class(CapyClass* cf, const std::string& fieldName, void* value)
{
    CapyField* f = capy_field_from_class(cf, fieldName);
    memcpy(f->SymHandle, value, type_size(f->FieldType));
}

void capy_field_data_set_from_field(CapyField* cf, void* value)
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
        ffiArgTypes[i] = get_ffi_type_p(localValues[i].Type);
        ffiArgValues[i] = get_ffi_arg_p(localValues[i]);
    }

    ffi_cif cif;
    ffi_type* ffiRet = get_ffi_type_p(method->ReturnType);

    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ffiRet, ffiArgTypes.data()) != FFI_OK)
    {
        std::cerr << "ffi_prep_cfi failed\n";
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
