#include "test-lib.h"
#include <stdio.h>
#include <iostream>


void* MyObject = nullptr;

void PrintObject()
{
    if (MyObject)
        std::cout << "Object Has Data : " << MyObject << " \n";
    else
        std::cout << "Object has no data!\n";
}

namespace DLL
{
    int NamespacedVar = 10;
    void Test::Print(const char* msg)
    {
        if (msg)
        {
            m_PreviousMessage = msg;
        }
        else
        {
            m_PreviousMessage = "(null)";
        }

        printf( "Got : %s\n", msg ? msg : "(null)");
    }

    void Test::PrintAgain()
    {
        printf( "Reprinting : %s\n", m_PreviousMessage);
    }

    Test* Test::Create()
    {
        Test* test = new Test();
        std::cout << "Creating a new Test object: " << test << std::endl;
        return test;
    }

    void Test2::PrintHello()
    {
        printf("Hello World");
    }

    Test2* Test2::Create()
    {
        Test2* test = new Test2();
        std::cout << "Creating a new Test object: " << test << std::endl;
        return test;
    }

    void ThisIsInANamespace()
    {

    }
}


void OutOfNamespace() {

}
