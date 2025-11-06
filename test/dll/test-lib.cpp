#include "test-lib.h"
#include <stdio.h>
#include <iostream>


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

    Test* Test::Create()
    {
        Test* test = new Test();
        std::cout << "Creating a new Test object: " << test << std::endl;
        return test;
    }
}

