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

    void Test::UpdateCheck(int16_t test) {}

    void Test::CustomAdd(int a, int b)
    {
        printf("%d + %d = %d\n", a, b, Add(a, b));
    }

    Test* Test::Create()
    {
        Test* test = new Test();
        std::cout << "Creating a new Test object: " << test << std::endl;
        return test;
    }
}

