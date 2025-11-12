#include "test-lib.h"
#include "base-class.h"
#include <stdio.h>
#include <iostream>

#include <dlfcn.h>


namespace DLL
{

    int NamespacedVar = 10;

    class InCPPFile
    {
    public:
        void Test() {}
    };

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

    void Test::Add(int a, int b)
    {
        printf("%d + %d = %d\n", a, b, Internal_Add(a, b));
        printf("%f + %f = %f\n", 1.5, 2.25, Internal_Add(1.5f, 2.25f));
    }

    Test* Test::Create()
    {
        Test* test = new Test();
        std::cout << "Creating a new Test object: " << test << std::endl;
        return test;
    }
}

