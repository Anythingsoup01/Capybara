#pragma once


namespace DLL 
{
    class Test 
    {
    public:
        void Print(const char* msg);
        void PrintAgain();

        static Test* Create();
    private:
        const char* m_PreviousMessage;
    };

    class Test2 
    {
    public:
        void PrintHello();

        static Test2* Create();
    };
}
