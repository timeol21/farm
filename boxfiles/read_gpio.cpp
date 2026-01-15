#include <iostream>
#include <fstream>
#include <unistd.h>

int main() {
    const char* gpio_value_path = "/sys/class/gpio/gpio33/value";   

    while (true) {
        std::ifstream gpio_value_file(gpio_value_path);
        if (!gpio_value_file.is_open()) {
            std::cerr << "Open GPIO33 's value files flaut,have or not have permission" << std::endl;
            return -1;
        }

        int value;
        gpio_value_file >> value;

        if (value == 0) {
            std::cout << "sensor status : ON(LOW)" << std::endl;
        } else if (value == 1) {
            std::cout << "sensor status : OFF(HIGH)" << std::endl;
        } else {
            std::cout << "UNKNOWN: " << value << std::endl;
        }

        gpio_value_file.close();

        usleep(500000); // 每 0.5 秒读取一次
    }

    return 0;
}
