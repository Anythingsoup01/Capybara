#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>



#include "Runtime.hpp"

class Capybara
{
public:
    static void InitCapy();

    static bool AddLibrary(const std::filesystem::path& filePath);

    static void* CallMethod(CapyObject* obj, const std::string& methodName, const std::vector<RuntimeValue>& values = {});

    static std::unique_ptr<CapyObject> CreateObject();
    static CapyObject* GetObject(const std::string& libName);

    static std::unique_ptr<ManagedString> CreateManagedString(const std::string& data);

private:

    static ValueType ParseTokenType(const std::string& token);
    static void ParseSignature(const std::string& demangledSignature, ValueType& outReturn, std::vector<ValueType>& outParams);
    static bool IsClassMethod(const std::string& demangledSignature);
    static MethodKind GetMethodKind(const std::string& demangled, const std::string& mangled);
    static void SetParameters(CapyType* type, const std::string& methodName, const ValueType& returnType = ValueType::VOID, const std::vector<ValueType>& params = {});

    static std::unordered_map<std::string, std::string> ProcessLibrary(const std::filesystem::path& filePath);

    static void RegisterMethod(CapyType* type, const std::string& name, void (*fn)(CapyObject*));
    static MethodEntry ConvertDeclaredToMethod(const DeclaredMethodEntry& d);

    static void BuildVTable(CapyType* type);

    static MethodEntry LoadExternalMethod(const std::string& name, void* handle);
    static void* CallExternalMethod(MethodEntry* method, const std::vector<RuntimeValue>& values);

    static void AllocateCapyObject(std::unique_ptr<CapyObject>& obj);
public:
    static MethodEntry* GetMethod(CapyObject* obj, const std::string& methodName);
    static MethodEntry* GetMethod(CapyType* type, const std::string& methodName);
private:
    static inline CoreTypeRegistry s_CoreRegistry = CoreTypeRegistry();
    static inline std::vector<std::unique_ptr<CapyObject>> s_LoadedLibraries = {};
};
