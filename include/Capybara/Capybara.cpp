#include "Capybara.h"
#include <dlfcn.h>
#include <link.h>
#include <iostream>
#include <libelf.h>
#include <gelf.h>
#include <fcntl.h>
#include <unistd.h>
#include <cxxabi.h>
#include <signal.h>

#define CPY_API_ASSERT(x, ...) if (!(x)) { printf("ERROR: %s", __VA_ARGS__); raise(SIGTRAP); }

namespace Capybara
{
    std::string demangle(const char* name)
    {
        int status = 0;
        // Call the ABI demangling function
        char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
        if (status == 0) {
            std::string result(demangled);
            free(demangled);
            return result;
        }
        // If demangling fails, return the original name
        return name;
    }

    Capybara::Capybara(const std::filesystem::path& filePath) // TODO: We should take a second argument for flags
    {
        m_Instance = dlopen(filePath.c_str(), RTLD_NOW);
        CPY_API_ASSERT(m_Instance, dlerror());

        std::unordered_map<std::string, std::string> symbols = ProcessLibrary(filePath);
        for (const auto& [demangledName, mangledName] : symbols)
        {
            CapybaraFunction func = (CapybaraFunction)dlsym(m_Instance, mangledName.c_str());
            const char* err = dlerror();
            if (err)
            {
                std::cout << "ERROR: " << err << std::endl;
                continue;
            }
            if (!func)
            {
                continue;
            }

            std::string tmp = demangledName;
            size_t firstParenthesi = tmp.find_first_of('(');
            tmp.erase(firstParenthesi);

            m_Functions[tmp] = func;
        }
    }

    Capybara::~Capybara()
    {
        dlclose(m_Instance);
        m_Instance = nullptr;
    }

    bool Capybara::FunctionExists(const std::string& functionName)
    {
        if (m_Functions.contains(functionName))
            return true;

        return false;
    }

    CapybaraFunction Capybara::RetrieveFunction(const char* nameSoace, const char* className, const char* functionName)
    {
        if (!functionName)
        {
            CPY_API_ASSERT(false, "Function Name not declared!");
            return nullptr;
        }
        std::string functionCall;

        if (nameSoace)
            functionCall.append(nameSoace).append("::");
        if (className)
            functionCall.append(className).append("::");

        functionCall.append(functionName);

        if (!FunctionExists(functionCall))
            return nullptr;

        return m_Functions[functionCall];

    }

    std::unordered_map<std::string, std::string> Capybara::ProcessLibrary(const std::filesystem::path& filePath)
    {
        if (elf_version(EV_CURRENT) == EV_NONE) {
            std::cerr << "ELF library initialization failed." << std::endl;
            return {};
        }

        int fd = open(filePath.c_str(), O_RDONLY);
        if (fd < 0) {
            perror("open");
            return {};
        }

        Elf* elf = elf_begin(fd, ELF_C_READ, nullptr);
        if (!elf) {
            std::cerr << "elf_begin() failed: " << elf_errmsg(-1) << std::endl;
            close(fd);
            return {};
        }

        std::unordered_map<std::string, std::string> out;

        Elf_Scn* scn = nullptr;
        while ((scn = elf_nextscn(elf, scn)) != nullptr)
        {
            GElf_Shdr shdr;
            if (gelf_getshdr(scn, &shdr) != &shdr) continue;

            if (shdr.sh_type == SHT_DYNSYM)
            {
                Elf_Data* data = elf_getdata(scn, nullptr);
                int symbol_count = shdr.sh_size / shdr.sh_entsize;

                std::cout << "Dynamic symbols for " << filePath << ":" << std::endl;
                for (int i = 0; i < symbol_count; ++i)
                {
                    GElf_Sym sym;
                    if (gelf_getsym(data, i, &sym) != &sym) continue;

                    if (sym.st_shndx == SHN_UNDEF)
                        continue;

                    const char* name = elf_strptr(elf, shdr.sh_link, sym.st_name);
                    if (name && *name) {
                        out[demangle(name)] = name;
                    }
                }
            }
        }

        elf_end(elf);
        close(fd);
        return out;
    }

}

