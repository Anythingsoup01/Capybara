#pragma once

#include <filesystem>
#include <unordered_map>

typedef void* CapybaraObject;
typedef CapybaraObject (*CapybaraFunction)(...);
typedef void CapybaraVariable;

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
    private:
        CapybaraObject m_Instance = nullptr;
        std::unordered_map<std::string, CapybaraFunction> m_Functions;
    };
}
