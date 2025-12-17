#include "base-class.h"
#include <stdio.h>

namespace DLL
{
    Base::Base()
    {
        printf("Base Class!\n");
    }

    int Base::Add(int a, int b)
    {
        return Internal_Add(a, b);
    }
}
