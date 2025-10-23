#include "Capybara.h"
#include <iostream>

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


}
