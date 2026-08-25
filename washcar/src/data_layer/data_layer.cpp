#include "data_layer/data_layer.h"

#include "data_layer/device_manager/device_manager.h"
#include "data_layer/database/database_manager.h"

DataLayer::DataLayer()
{

}


DataLayer::~DataLayer()
{

}

bool DataLayer::initialize()
{

    deviceManager_ = std::make_unique<DeviceManager>();

    databaseManager_ = std::make_unique<DatabaseManager>();

    if(!deviceManager_->initialize())
    {
        return false;
    }

    if(!databaseManager_->initialize())
    {
        return false;
    }

    return true;

}

DeviceManager& DataLayer::deviceManager()
{
    return *deviceManager_;
}

DatabaseManager& DataLayer::databaseManager()
{
    return *databaseManager_;
}