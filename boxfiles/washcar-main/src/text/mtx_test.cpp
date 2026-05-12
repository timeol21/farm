#include <iostream>
#include <mutex>
#include <chrono>
#include <thread>

using namespace std;

class Calculate
{
    int m_i;
    recursive_mutex mtx;

public:
    Calculate():m_i(6){}
    void mul()
    {
        mtx.lock();
        m_i *= 2;
    }
    void div()
    {
        mtx.lock();
        m_i /= 2;
    }
    void test()
    {
        mul();
        div();
        show();
    }
    void show()
    {
        cout << m_i << endl;
    }
};

int main()
{
    Calculate c;
    c.test();
    return 0;
}
