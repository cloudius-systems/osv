#include <thread>
#include <chrono>

static int global = 0;

int main(int argc, char **argv)
{
    if (global) {
        return 1;
    }

    global = 1;

    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
}
