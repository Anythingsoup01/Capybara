#pragma once
#include "base-class.h"

namespace DLL
{
    class Test : public Base
    {
    public:
        void Print(const char* msg) override;
        void PrintAgain() override;

        static Test* Create();
    private:
        const char* m_PreviousMessage;
    };
}
