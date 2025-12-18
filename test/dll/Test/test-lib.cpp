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

    void Test::PrintBaseInt()
    {
        printf("Got: %u\n", m_Base.ID);
    }

    void Test::Print(const char* msg)
    {
    }

    void Test::PrintAgain()
    {
    }

    void Test::UpdateCheck(int16_t test) {}

    void Test::CustomAdd(int a, int b)
    {
        printf("%d + %d = %d\n", a, b, Add(a, b));
    }

}

