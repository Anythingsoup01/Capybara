#pragma once

typedef void CapybaraVariable;

#include <string>
#include <vector>


namespace Capybara 
{
    struct CapyType;

    struct CapyObject
    {
        CapyType* Type;
    };

    struct MethodEntry
    {
        std::string Name;
        void (*fn)(CapyObject*);
    };

    struct CapyType
    {
        std::string Name;
        CapyType* Parent;
        unsigned int InstanceSize;
        std::vector<MethodEntry> vtable;

        CapyType(const std::string& name, CapyType* parent, size_t size)
            : Name(name), Parent(parent), InstanceSize(size) {}
    };

    struct CoreTypeRegistry
    {
        CapyType* Object;
        CapyType* String;
        CapyType* Int32;
        CapyType* Array;
        std::vector<CapyType*> Customs;
    };
}
