#include "Capybara.h"
#include <iostream>

int main(int argc, char** argv) 
{
    Capybara::Capybara instance = Capybara::Capybara("test/dll/test-lib.so");

    const CapybaraFunction& printFunc = instance.RetrieveFunction("DLL", "Test", "Print");
    const CapybaraFunction& printAgainFunc = instance.RetrieveFunction("DLL", "Test", "PrintAgain");
    
    CapybaraObject obj = instance.RetrieveFunction("DLL", "Test", "Create")();

    if (obj)
    {
        instance.RetrieveFunction("DLL", "Test", "Print")(obj, "This is a test!");
        instance.RetrieveFunction("DLL", "Test", "PrintAgain")(obj);
    }

}
