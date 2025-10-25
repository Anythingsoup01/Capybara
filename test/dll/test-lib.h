#pragma once


namespace DLL 
{
    class Test 
    {
    public:
        void Print(const char* msg);
        void PrintAgain();
        void Add(int a, int b);

        static Test* Create();
    private:
        const char* m_PreviousMessage;
    };
}
