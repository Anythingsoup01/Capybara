#pragma once

#include <filesystem>
#include <unordered_map>



#include "Runtime.hpp"

class Capybara
{
public:
    static void InitCapy();
    static void AddLibrary(const std::filesystem::path& filePath);
    static void* CallMethod(CapyObject* obj, const std::string& methodName, const std::vector<RuntimeValue>& values = {});
    static CapyObject* GetObject(const std::string& libName);

    static std::unique_ptr<CapyObject> CreateObject();
    static std::unique_ptr<ManagedString> CreateManagedString(const std::string& data);
private:
    static std::unordered_map<std::string, std::string> ProcessLibrary(const std::filesystem::path& filePath);
    static void RegisterMethod(CapyType* type, const std::string& name, void (*fn)(CapyObject*));
    static MethodEntry ConvertDeclaredToMethod(const DeclaredMethodEntry& d);
    static void BuildVTable(CapyType* type);
    static void* CallExternalMethod(CapyObject* obj, const std::string& methodName, const std::vector<RuntimeValue>& values);
private:
    static inline CoreTypeRegistry s_CoreRegistry = CoreTypeRegistry();
    static inline std::vector<CapyObject> s_LoadedLibraries = {};
};
