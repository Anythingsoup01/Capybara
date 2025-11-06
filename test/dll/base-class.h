#pragma once

namespace DLL
{
    class Base
    {
    public:
        Base();
        virtual void Print(const char* msg) = 0;
        virtual void PrintAgain() = 0;
    };
}
