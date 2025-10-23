#pragma once

namespace DLL 
{
    class Test 
    {
    public:
        static void Print(const char* msg);
        void PrintAgain();

        static Test* Create();
    private:
        const char* m_PreviousMessage;
    };
}
