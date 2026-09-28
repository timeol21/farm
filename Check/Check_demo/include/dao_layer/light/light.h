#ifndef LIGHT_H
#define LIGHT_H

#include <string>


class GpioLight
{
public:

    explicit GpioLight(int gpio_pin);

    bool init();

    bool turnOn();

    bool turnOff();

    bool setState(bool on);

    bool getState();


private:

    bool gpio_export();

    bool gpio_set_direction(const std::string& dir);


    int gpio_read_value();


    bool gpio_write_value(int value);



private:

    int gpio_pin_;

    const std::string gpio_path_ = "/sys/class/gpio";

};


#endif