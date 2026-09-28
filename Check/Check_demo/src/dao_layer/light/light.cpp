#include "dao_layer/light/light.h"

#include <iostream>
#include <fstream>
#include <unistd.h>

using namespace std;

GpioLight::GpioLight(int gpio_pin)
    : gpio_pin_(gpio_pin)
{

}

bool GpioLight::init()
{

    if(!gpio_export())
    {
        return false;
    }


    if(!gpio_set_direction("out"))
    {
        return false;
    }


    int current = gpio_read_value();


    if(current == 1)
    {

        std::cout
        << "Light is ON during initialization, turning OFF."
        << std::endl;

    }
    else
    {

        std::cout
        << "Light is already OFF during initialization."
        << std::endl;

    }


    // 无论初始状态是什么
    // 初始化后统一关闭
    gpio_write_value(0);



    std::cout
    << "GPIO light initialization completed."
    << std::endl;


    return true;
}

bool GpioLight::turnOn()
{
    return gpio_write_value(1);
}

bool GpioLight::turnOff()
{
    return gpio_write_value(0);
}

bool GpioLight::setState(bool on)
{

    if(on)
    {
        return turnOn();
    }
    else
    {
        return turnOff();
    }

}

bool GpioLight::getState()
{

    int value = gpio_read_value();

    return value == 1;

}

bool GpioLight::gpio_export()
{

    string path = gpio_path_  + "/gpio" + to_string(gpio_pin_);

    if(ifstream(path.c_str()).good())
    {
        return true;
    }

    ofstream ofs(gpio_path_ + "/export");

    if(!ofs.is_open())
    {
        std::cerr << "Failed to export GPIO. Please run as root."
             << std::endl;

        return false;
    }

    ofs << gpio_pin_;

    ofs.close();

    usleep(100000);

    return true;

}

bool GpioLight::gpio_set_direction(const string& dir)
{

    string path = gpio_path_ + "/gpio" + to_string(gpio_pin_) + "/direction";

    ofstream ofs(path.c_str());

    if(!ofs.is_open())
    {
        return false;
    }

    ofs << dir;

    ofs.close();

    return true;

}

int GpioLight::gpio_read_value()
{

    string path = gpio_path_ + "/gpio"
                + to_string(gpio_pin_)
                + "/value";

    ifstream ifs(path.c_str());


    if(!ifs.is_open())
    {
        std::cerr << "Failed to read GPIO value."
             << std::endl;

        return -1;
    }


    int value;

    ifs >> value;


    ifs.close();


    return value;
}

bool GpioLight::gpio_write_value(int value)
{


    if(value != 0 && value != 1)
    {
        std::cerr << "Invalid GPIO value. Only 0 or 1 is allowed."
             << std::endl;

        return false;
    }
    
    string dir_path = gpio_path_
                    + "/gpio"
                    + to_string(gpio_pin_)
                    + "/direction";


    ifstream dir_file(dir_path.c_str());

    string current_dir;

    if(dir_file.is_open())
    {
        dir_file >> current_dir;
        dir_file.close();
    }else {
            std::cerr << "dir_file is close "<< std::endl;
    }

    if(current_dir == "in")
    {

        std::cerr 
        << "GPIO is configured as input mode. "
        << "Write operation rejected."
        << std::endl;


        return false;

    }

    string path = gpio_path_
                + "/gpio"
                + to_string(gpio_pin_)
                + "/value";



    ofstream ofs(path.c_str());


    if(!ofs.is_open())
    {
        std::cerr << "Failed to write GPIO value."
             << std::endl;

        return false;
    }else {
        std::cerr << "Sucess to write GPIO value."
             << std::endl;
    }

    ofs << value;
    
     if(!ofs.good())
    {
        std::cerr
        << "Failed to write GPIO value."
        << std::endl;

        ofs.close();

        return false;
    }


    ofs.close();



    return true;

}