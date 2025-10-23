#include "test-lib.h"
#include <stdio.h>

namespace DLL
{
    void Test::Print(const char* msg)
    {
        printf( "Got :%s\n", msg);
    }

    void Test::PrintAgain()
    {
        printf( "Reprinting : %s\n", m_PreviousMessage);
    }

    Test* Test::Create()
    {
        return new Test();
    }
}
