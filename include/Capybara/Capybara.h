#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>



#include "Runtime.hpp"

class Capybara
{
public:
    static void InitCapy();
    static void ShutdownCapy();

    static bool AddLibrary(const std::filesystem::path& filePath);

    static CapyObject* GetClassObject(const std::string& classNamespace, const std::string& className);

    static void* CallMethod(CapyObject* obj, const std::string& methodName, const std::vector<RuntimeValue>& values = {});

    static std::unique_ptr<CapyObject> CreateObject();
    static CapyObject* GetObject(const std::string& libName);

    static std::unique_ptr<ManagedString> CreateManagedString(const std::string& data);

private:
    static std::vector<ValueType> SetParameters(const std::string demangledName, MethodKind kind, const std::vector<std::string>& parameters);

    static std::vector<Symbol> ProcessDwarf(const std::filesystem::path& filePath);
    static std::unordered_map<std::string, std::string> ProcessLibrary(const std::filesystem::path& filePath);

    static void RegisterMethod(CapyClass* type, const std::string& name, void (*fn)(CapyObject*));
    static MethodEntry ConvertDeclaredToMethod(const DeclaredMethodEntry& d);

    static void BuildVTable(CapyClass* type);

    static MethodEntry LoadExternalMethod(const std::string& name, void* handle);
    static void* CallExternalMethod(MethodEntry* method, const std::vector<RuntimeValue>& values);

    static void AllocateCapyObject(std::unique_ptr<CapyObject>& obj);
public:
    static MethodEntry* GetMethod(CapyObject* obj, const std::string& methodName);
    static MethodEntry* GetMethod(CapyClass* type, const std::string& methodName);
private:
    static inline CoreTypeRegistry s_CoreRegistry = CoreTypeRegistry();
    static inline std::vector<std::unique_ptr<CapyObject>> s_LoadedLibraries = {};
};
