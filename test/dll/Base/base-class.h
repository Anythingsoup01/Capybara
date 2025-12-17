#pragma once
#include "internal_calls.h"


namespace DLL
{
    struct DataStruct
    {
        int Daddy;
    };

    class Base
    {
    public:
        Base();

        int Add(int a, int b);

        DataStruct m_Data;

    protected:
        int m_TestInteger;
        int m_Test2;
        int m_Test3;

    };
}
