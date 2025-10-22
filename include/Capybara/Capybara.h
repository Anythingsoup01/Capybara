#pragma once

#include <filesystem>
#include <unordered_map>

typedef void* CapybaraObject;
typedef CapybaraObject (*CapybaraFunction)(...);
typedef void CapybaraVariable;

#include "Runtime.hpp"

namespace Capybara
{
    class Capybara
    {
    public:
        Capybara(const std::filesystem::path& filePath);
        ~Capybara();

        bool FunctionExists(const std::string& functionName);
        CapybaraFunction RetrieveFunction(const char* nameSpace, const char* className, const char* functionCall);

    private:
        std::unordered_map<std::string, std::string> ProcessLibrary(const std::filesystem::path& filePath);
        std::unique_ptr<CapyObject> CreateObject(CapyType* type);
        static void ObjectToString(CapyObject* obj);
        void CallMethod(CapyObject* obj, const std::string& methodName);
    private:
        CapybaraObject m_Instance = nullptr;
        std::unordered_map<std::string, CapybaraFunction> m_Functions;
    };
}
