#pragma once
#include <memory>

class HallService;

class BusinessLayer
{

public:

    static BusinessLayer& instance();

    bool initialize();

    HallService& hallService();


private:

    BusinessLayer() = default;

    std::unique_ptr<HallService> hallService_;

};