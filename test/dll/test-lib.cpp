#include "test-lib.h"
#include <stdio.h>
#include <iostream>

namespace DLL
{
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

        printf( "Got :%s\n", msg ? msg : "(null)");
    }

    void Test::PrintAgain()
    {
        printf( "Reprinting : %s\n", m_PreviousMessage);
    }

    void Test::Add(int a, int b)
    {
        printf("Result: %d", a + b);
    }

    Test* Test::Create()
    {
        Test* test = new Test();
        std::cout << "Creating a new Test object: " << test << std::endl;
        return test;
    }
}

