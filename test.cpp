#include <iostream>
#include <chrono>
#include <thread>

void test(int time) {
    std::this_thread::sleep_for(std::chrono::seconds(time));
    std::cout << "done" << std::endl;
}

int main()
{
    std::thread t(test, 2);
    std::thread t2(test, 2);
    t.join();
    t2.join();
    std::cout << "test" << std::endl;
    return 0;
}