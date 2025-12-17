#pragma once
#include "internal_calls.h"


namespace DLL
{
    class Base
    {
    public:
        Base();
        virtual void Print(const char* msg) = 0;
        virtual void PrintAgain() = 0;

        int Add(int a, int b)
        {
            return Internal_Add(a, b);
        }
    private:
        int m_TestInteger;
    };
}
