#include "test-lib.h"
#include "base-class.h"
#include <stdio.h>
#include <iostream>

#include <dlfcn.h>

namespace DLL
{
    void Test::PrintBaseInt()
    {
        printf("Got: %u\n", m_Base.ID);
    }

    void Test::CustomAdd(int a, int b)
    {
        printf("%d + %d = %d\n", a, b, Add(a, b));
    }


}

