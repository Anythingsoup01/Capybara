#include "Capybara.h"
#include "Runtime.hpp"
#include <iostream>

#include <dlfcn.h>
#include <ffi.h>


int main(int argc, char** argv) 
{
    Capybara::InitCapy();

    auto obj = Capybara::CreateObject();
    Capybara::CallMethod(obj.get(), "ToString");


    auto s = Capybara::CreateManagedString("Hello from Managed String!");
    Capybara::CallMethod(s.get(), "ToString");


    void* valPtr = Capybara::CallMethod(static_cast<CapyObject*>(s.get()), "GetValue");
    if (valPtr)
    {
        auto strPtr = reinterpret_cast<std::string*>(valPtr);
        std::cout << "From the GetValue(): " << *strPtr << std::endl;
    }

    Capybara::AddLibrary("test/dll/test-lib.so");

    auto testLib = Capybara::GetObject("test-lib.so");
    void* classOBJ = Capybara::CallMethod(testLib, "DLL::Test::Create");

    RuntimeValue classValue;
    classValue.Type = ValueType::OBJECT;
    classValue.obj = classOBJ;

    std::string data = "This is a test!";
    RuntimeValue stringValue;
    stringValue.Type = ValueType::STRING;
    stringValue.obj = (void*)data.c_str();

    Capybara::CallMethod(testLib, "DLL::Test::Print", { classValue,  stringValue });

    Capybara::CallMethod(testLib, "DLL::Test::PrintAgain", { classValue });

    Capybara::CallMethod(testLib, "DLL::Test::Add", { classValue, RuntimeValue(10), RuntimeValue(12) });

    return 0;
}
