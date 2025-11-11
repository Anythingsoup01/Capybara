#include "base-class.h"
#include <stdio.h>

Internal_Add_func_t Internal_Add = nullptr;

namespace DLL
{
    Base::Base()
    {
        printf("Base Class!\n");
    }
}
