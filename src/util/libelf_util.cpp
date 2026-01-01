#include "util/libelf_util.h"

#include "util/string_util.h"
#include "util/cxxabi_util.h"

// Get the name of a type DIE
static CapyString get_short_name(const dwarf::die& die, CapyDomain* domain)
{
    // fallback: mangled name
    if (die.has(dwarf::DW_AT::name))
        return capy_string_arena(domain->Arena, die[dwarf::DW_AT::name].as_string().c_str());
    // fallback: mangled name
    if (die.has(dwarf::DW_AT::linkage_name))
        return capy_string_arena(domain->Arena, die[dwarf::DW_AT::linkage_name].as_string().c_str());

    return capy_string_literal("<anon>");
}

static CapyString resolve_type(const dwarf::die& type_die, CapyDomain* domain)
{
    auto tag = type_die.tag;

    if (type_die.has(dwarf::DW_AT::name))
        return capy_string_arena(domain->Arena, type_die[dwarf::DW_AT::name].as_string().c_str());

    switch(tag) {
        case dwarf::DW_TAG::pointer_type:
            if (type_die.has(dwarf::DW_AT::type))
            {
                CapyString base = resolve_type(type_die[dwarf::DW_AT::type].as_reference(), domain);
                return capy_string_arena(domain->Arena, (std::string(base.c_str()) + "*").c_str());
            }
            return capy_string_literal("void*");
        case dwarf::DW_TAG::const_type:
            if (type_die.has(dwarf::DW_AT::type))
            {
                CapyString base = resolve_type(type_die[dwarf::DW_AT::type].as_reference(), domain);
                return capy_string_arena(domain->Arena, (std::string(base.c_str()) + " const").c_str());
            }
            return capy_string_literal("const");
        case dwarf::DW_TAG::reference_type:
            if (type_die.has(dwarf::DW_AT::type))
            {
                CapyString base = resolve_type(type_die[dwarf::DW_AT::type].as_reference(), domain);
                return capy_string_arena(domain->Arena, (std::string(base.c_str()) + "&").c_str());
            }
            return capy_string_literal("<ref>");
        case dwarf::DW_TAG::rvalue_reference_type:
            if (type_die.has(dwarf::DW_AT::type))
            {
                CapyString base = resolve_type(type_die[dwarf::DW_AT::type].as_reference(), domain);
                return capy_string_arena(domain->Arena, (std::string(base.c_str()) + "&&").c_str());
            }
            return capy_string_literal("<rref>");
        default:
            return capy_string_literal("<unnamed-type>");
    }
}

static CapyString get_return_type(const dwarf::die& die, CapyDomain* domain)
{
    if (die.has(dwarf::DW_AT::type)) {
        try {
            dwarf::die typeDie = die[dwarf::DW_AT::type].as_reference();
            return resolve_type(typeDie, domain);
        } catch (...) {
            return capy_string_literal("void*"); // fallback to generic pointer
        }
    }
    // DWARF omitted return type — assume unknown, default to void*
    return capy_string_literal("void*");
} 

static inline CapyString join_scope(const std::vector<CapyString>& scope_stack, CapyDomain* domain)
{
    if (scope_stack.empty())
        return capy_string_literal("");

    std::vector<CapyString> goodStack;
    for (auto& scope : scope_stack)
    {
        if (scope.Data)
            goodStack.push_back(scope);
    }

    std::string tmp;
    for (size_t i = 0; i < goodStack.size(); ++i)
    {
        if (i > 0) tmp += "::";
        tmp += goodStack[i].Data; // copy from arena/literal string
    }

    return capy_string_arena(domain->Arena, tmp.c_str());
}

void traverse_and_collect(const dwarf::die& d, std::vector<CapyString>& scope_stack, RuntimeStorage& storage, std::vector<_SymbolMetaData>& outSymbols)
{
    auto& cfg = storage.Config;
    auto* domain = storage.Active.Runtime.get();
    CapyString name = get_short_name(d, domain);

    bool is_namespace = d.tag == dwarf::DW_TAG::namespace_;
    bool is_classname = d.tag == dwarf::DW_TAG::class_type;
    bool is_structure = d.tag == dwarf::DW_TAG::structure_type;
    bool is_union = d.tag == dwarf::DW_TAG::union_type;

    bool is_scope = is_namespace || is_classname || is_structure || is_union;

    if (is_namespace && !name.empty() && m_strs_n_equal(name, cfg.IgnoredNamespaces))
        return;
    if (is_classname && !name.empty() && m_strs_n_equal(name, cfg.IgnoredClassNames))
        return;
    if (is_structure && !name.empty() && m_strs_n_equal(name, cfg.IgnoredClassNames))
        return;

    if (is_scope && !name.empty())
        scope_stack.push_back(name);


    if (is_classname || is_structure)
    {
        std::vector<BaseClass> classes;
        for (auto& child : d)
        {
            if (child.tag == dwarf::DW_TAG::inheritance && child.has(dwarf::DW_AT::type))
            {
                dwarf::die base_die = child[dwarf::DW_AT::type].as_reference();
                CapyString base_name = get_short_name(base_die, domain);
                
                size_t vectorSize = scope_stack.size();

                std::vector<CapyString> namespaceScope(scope_stack.begin(), scope_stack.end() - 1);
                CapyString qualified_name = join_scope(namespaceScope, domain);


                classes.push_back({ qualified_name, base_name});
                if (is_classname)
                    storage.Config.KnownClassNames[generate_hash(base_name.c_str())] = base_name;
                else
                    storage.Config.KnownStructNames[generate_hash(base_name.c_str())] = base_name;
            }
        }

        if (is_classname)
            storage.Config.KnownClassNames[generate_hash(name.c_str())] = name;
        else
            storage.Config.KnownStructNames[generate_hash(name.c_str())] = name;

        CapyString full_scope = join_scope(scope_stack, domain);

        uint32_t nameHash = generate_hash(full_scope.c_str());

        if (is_classname)
            storage.Config.ClassMap[nameHash] = classes;
        else
            storage.Config.StructMap[nameHash] = classes;
    }


    // ---- Process functions ----
    if (d.tag == dwarf::DW_TAG::subprogram) {
        if (scope_stack.empty() && cfg.IgnoreEmptyNamespaces) return;
        if (strncmp(name.c_str(), "_ZN", 3) == 0) return;

        CapyString qualified_name = join_scope(scope_stack, domain);

        _SymbolMetaData sym;
        if (strs_n_equal(name.c_str(), {"<anon>"})) return;

        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = get_return_type(d, domain);
        sym.IsVariable = false;
        sym.IsClassInstance = false;
        sym.Offset = 0;



        for (auto& child : d)
        {
            if (child.tag != dwarf::DW_TAG::formal_parameter) continue;
            if (!child.has(dwarf::DW_AT::type)) continue;

            CapyString paramType = resolve_type(child[dwarf::DW_AT::type].as_reference(), domain);
            std::string paramTypeStr(paramType.c_str());

            size_t pointer = paramTypeStr.rfind("*");
            if (pointer != std::string::npos)
            {
                uint32_t typeNameHash = generate_hash(paramTypeStr.substr(0, pointer).c_str());

                std::vector<CapyString> fullScope(scope_stack);

                size_t scopeSize = fullScope.size();

                if (scopeSize < 2)
                {
                    sym.ParameterTypes.push_back(paramType);
                    continue;
                }

                CapyString className(fullScope[scopeSize - 2]);

                uint32_t classHash = generate_hash(className.c_str());

                if (typeNameHash == classHash)
                {
                    auto it = cfg.KnownClassNames.find(classHash);
                    if (it == cfg.KnownClassNames.end())
                    {
                        cfg.KnownClassNames[classHash] = className;
                    }

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

        std::vector<CapyString> fullScope(scope_stack);

        size_t scopeSize = fullScope.size();

        if (scopeSize >= 1)
        {
            CapyString className = fullScope[scopeSize - 1];

            uint32_t classHash = generate_hash(className.c_str());

            bool found = false;

            {
                auto it = cfg.KnownClassNames.find(classHash);
                if (it != cfg.KnownClassNames.end())
                {
                    sym.ClassName = it->second;
                    size_t nsLen = sym.Namespace.length();
                    size_t clsLen = className.length();

                    if (nsLen > clsLen + 1)   // ensure "A::B" minimum
                    {
                        size_t pos = nsLen - clsLen - 2;
                        sym.Namespace = capy_string_arena(domain->Arena,
                                std::string(sym.Namespace.c_str(), pos).c_str());
                    }
                    else
                    {
                        sym.Namespace = capy_string_literal("");
                    }

                    sym.IsStruct = false;
                    found = true;
                }
            }

            if (!found)
            {
                auto it = cfg.KnownStructNames.find(classHash);
                if (it != cfg.KnownStructNames.end())
                {
                    sym.ClassName = it->second;
                    size_t nsLen = sym.Namespace.length();
                    size_t clsLen = className.length();

                    if (nsLen > clsLen + 1)   // ensure "A::B" minimum
                    {
                        size_t pos = nsLen - clsLen - 2;
                        sym.Namespace = capy_string_arena(domain->Arena,
                                std::string(sym.Namespace.c_str(), pos).c_str());
                    }
                    else
                    {
                        sym.Namespace = capy_string_literal("");
                    }

                    sym.IsStruct = true;
                    found = true;
                }
            }
        }

        outSymbols.push_back(sym);

    }


    // ---- Process member variables ----
    if (d.tag == dwarf::DW_TAG::member)
    {
        CapyString qualified_name = join_scope(scope_stack, domain);


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
        _SymbolMetaData sym = _SymbolMetaData();
        CapyString name = get_short_name(d, domain);
        if (m_strs_n_equal(name, cfg.IgnoredNames)) return;


        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = get_return_type(d, domain);
        sym.IsVariable = true;
        sym.IsClassInstance = true;
        sym.Offset = offset;


        std::vector<CapyString> fullScope(scope_stack);

        size_t scopeSize = fullScope.size();

        if (scopeSize >= 1)
        {
            CapyString className = fullScope[scopeSize - 1];

            uint32_t classHash = generate_hash(className.c_str());

            bool found = false;

            {
                auto it = cfg.KnownClassNames.find(classHash);
                if (it != cfg.KnownClassNames.end())
                {
                    sym.ClassName = it->second;

                    size_t nsLen = sym.Namespace.length();
                    size_t clsLen = className.length();

                    if (nsLen > clsLen + 1)   // ensure "A::B" minimum
                    {
                        size_t pos = nsLen - clsLen - 2;
                        sym.Namespace = capy_string_arena(domain->Arena,
                                std::string(sym.Namespace.c_str(), pos).c_str());
                    }
                    else
                    {
                        sym.Namespace = capy_string_literal("");
                    }
                    sym.IsStruct = false;
                    found = true;
                }
            }

            if (!found)
            {
                auto it = cfg.KnownStructNames.find(classHash);
                if (it != cfg.KnownStructNames.end())
                {
                    sym.ClassName = it->second;
                    size_t nsLen = sym.Namespace.length();
                    size_t clsLen = className.length();

                    if (nsLen > clsLen + 1)   // ensure "A::B" minimum
                    {
                        size_t pos = nsLen - clsLen - 2;
                        sym.Namespace = capy_string_arena(domain->Arena,
                                std::string(sym.Namespace.c_str(), pos).c_str());
                    }
                    else
                    {
                        sym.Namespace = capy_string_literal("");
                    }
                    sym.IsStruct = true;
                    found = true;
                }
            }
        }

        outSymbols.push_back(sym);
    }

    if (d.tag == dwarf::DW_TAG::variable)
    {
        if (scope_stack.empty() && cfg.IgnoreEmptyNamespaces)
            return;

        CapyString qualified_name = join_scope(scope_stack, domain);

        // Intentionally leaving the class name empty
        // If we get an instance of Class* this,
        // then we will convert it to the ClassName.
        _SymbolMetaData sym;
        CapyString name = get_short_name(d, domain);
        if (strs_n_equal(name.c_str(), {"<anon>"})) return;

        sym.Name = name;
        sym.Namespace = qualified_name;
        sym.ReturnType = get_return_type(d, domain);
        sym.IsVariable = true;
        sym.IsClassInstance = false;
        sym.Offset = 0;
        std::vector<CapyString> fullScope(scope_stack);

        size_t scopeSize = fullScope.size();

        if (scopeSize >= 1)
        {
            CapyString className = fullScope[scopeSize - 1];

            uint32_t classHash = generate_hash(className.c_str());

            bool found = false;

            {
                auto it = cfg.KnownClassNames.find(classHash);
                if (it != cfg.KnownClassNames.end())
                {
                    sym.ClassName = it->second;
                    size_t nsLen = sym.Namespace.length();
                    size_t clsLen = className.length();

                    if (nsLen > clsLen + 1)   // ensure "A::B" minimum
                    {
                        size_t pos = nsLen - clsLen - 2;
                        sym.Namespace = capy_string_arena(domain->Arena,
                                std::string(sym.Namespace.c_str(), pos).c_str());
                    }
                    else
                    {
                        sym.Namespace = capy_string_literal("");
                    }
                    sym.IsStruct = false;
                    found = true;
                }
            }

            if (!found)
            {
                auto it = cfg.KnownStructNames.find(classHash);
                if (it != cfg.KnownStructNames.end())
                {
                    sym.ClassName = it->second;
                    size_t nsLen = sym.Namespace.length();
                    size_t clsLen = className.length();

                    if (nsLen > clsLen + 1)   // ensure "A::B" minimum
                    {
                        size_t pos = nsLen - clsLen - 2;
                        sym.Namespace = capy_string_arena(domain->Arena,
                                std::string(sym.Namespace.c_str(), pos).c_str());
                    }
                    else
                    {
                        sym.Namespace = capy_string_literal("");
                    }
                    sym.IsStruct = true;
                    found = true;
                }
            }
        }
        outSymbols.push_back(sym);
    }

    // ---- Recurse into children ----
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
        CapyString fullName = join_scope({ sym.Namespace, sym.ClassName, sym.Name }, storage.Runtime.get());

        auto it = symbolNames.find(fullName.c_str());
        if (it != symbolNames.end())
        {
            _SymbolMetaData resolved = sym;
            resolved.Signature = capy_string_arena(storage.Runtime->Arena, it->second.c_str());
            tmp.push_back(resolved);
        }
    }


    for (auto sym : symbols)
    {
        if (sym.Offset >= 0 && (sym.IsClassInstance && sym.IsVariable))
        {
            tmp.push_back(sym);
        }
    }

    return tmp;
}
