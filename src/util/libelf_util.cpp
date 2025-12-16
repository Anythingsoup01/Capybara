#include "util/libelf_util.h"

#include "util/string_util.h"
#include "util/cxxabi_util.h"

// Get the name of a type DIE
std::string get_short_name(const dwarf::die& die)
{
    // fallback: mangled name
    if (die.has(dwarf::DW_AT::name))
        return die[dwarf::DW_AT::name].as_string();
    // fallback: mangled name
    if (die.has(dwarf::DW_AT::linkage_name))
        return die[dwarf::DW_AT::linkage_name].as_string();

    return "<anon>";
}

std::string resolve_type(const dwarf::die& type_die)
{
    auto tag = type_die.tag;

    if (type_die.has(dwarf::DW_AT::name))
        return type_die[dwarf::DW_AT::name].as_string();

    switch(tag) {
        case dwarf::DW_TAG::pointer_type:
            if (type_die.has(dwarf::DW_AT::type))
                return resolve_type(type_die[dwarf::DW_AT::type].as_reference()) + "*";
            return "void*";
        case dwarf::DW_TAG::const_type:
            if (type_die.has(dwarf::DW_AT::type))
                return resolve_type(type_die[dwarf::DW_AT::type].as_reference()) + " const";
            return "const";
        case dwarf::DW_TAG::reference_type:
            if (type_die.has(dwarf::DW_AT::type))
                return resolve_type(type_die[dwarf::DW_AT::type].as_reference()) + "&";
            return "<ref>";
        case dwarf::DW_TAG::rvalue_reference_type:
            if (type_die.has(dwarf::DW_AT::type))
                return resolve_type(type_die[dwarf::DW_AT::type].as_reference()) + "&&";
            return "<rref>";
        default:
            return "<unnamed-type>";
    }
}

std::string get_return_type(const dwarf::die& die)
{
    if (die.has(dwarf::DW_AT::type)) {
        try {
            dwarf::die typeDie = die[dwarf::DW_AT::type].as_reference();
            return resolve_type(typeDie);
        } catch (...) {
            return "void*"; // fallback to generic pointer
        }
    }
    // DWARF omitted return type — assume unknown, default to void*
    return "void*";
} 

void traverse_and_collect(const dwarf::die& d, std::vector<std::string>& scope_stack, RuntimeStorage& storage, std::vector<_SymbolMetaData>& outSymbols)
{
    auto& cfg = storage.Config;
    std::string name = get_short_name(d);

    // Keep track of scopes
    bool is_namespace = d.tag == dwarf::DW_TAG::namespace_;
    bool is_classname = d.tag == dwarf::DW_TAG::class_type;
    bool is_structure = d.tag == dwarf::DW_TAG::structure_type;
    bool is_union = d.tag == dwarf::DW_TAG::union_type;

    bool is_scope = is_namespace || is_classname || is_structure || is_union;

    if (is_namespace && !name.empty())
    {
        if (strs_n_equal(name, cfg.IgnoredNamespaces))
            return;
    }
    if (is_classname && !name.empty())
    {
        if (strs_n_equal(name, cfg.IgnoredClassNames))
            return;
    }
    if (is_structure)
    {
        // TODO: IMPLEMENT THIS
        return;
    }

    if (is_scope && !name.empty())
        scope_stack.push_back(name);

    // Process functions
    if (d.tag == dwarf::DW_TAG::subprogram) {

        if (scope_stack.empty() && cfg.IgnoreEmptyNamespaces)
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
        _SymbolMetaData sym;
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
                    for (auto& name : cfg.KnownClassNames)
                    {
                        if (name == typeName)
                        {
                            sym.IsClassInstance = true;
                            continue;
                        }
                    }
                    cfg.KnownClassNames.push_back(typeName);
                    sym.IsClassInstance = true;
                }
                else
                {
                    sym.ParameterTypes.push_back(paramType);
                }

            }
            else
            {
                if (paramType.empty())
                    continue;

                sym.ParameterTypes.push_back(paramType);
            }
        }

        outSymbols.push_back(sym);

    }

    if (d.tag == dwarf::DW_TAG::member)
    {
        if (scope_stack.empty() && cfg.IgnoreEmptyNamespaces)
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
        _SymbolMetaData sym;
        std::string name = get_short_name(d);
        if (strs_n_equal(name, cfg.IgnoredNames))
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
        if (scope_stack.empty() && cfg.IgnoreEmptyNamespaces)
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
        _SymbolMetaData sym;
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
        traverse_and_collect(child, scope_stack, storage, outSymbols);

    if (is_scope && !name.empty())
        scope_stack.pop_back();
}

std::vector<_SymbolMetaData> process_library(const elf::elf& ef, const std::vector<_SymbolMetaData>& symbols, CapyActiveDomain& storage)
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


    std::vector<_SymbolMetaData> tmp;
    
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
            _SymbolMetaData resolved = sym;
            resolved.Signature = it->second;
            tmp.push_back(resolved);
        }
    }


    for (auto sym : symbols)
    {
        if (sym.Offset >= 0 && (sym.IsClassInstance && sym.IsVariable))
        {
            tmp.push_back(sym);
            continue;
        }
    }

    return tmp;
}
