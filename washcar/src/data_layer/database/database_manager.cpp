#include "data_layer/database/database_manager.h"

DatabaseManager::DatabaseManager()
{

}


DatabaseManager::~DatabaseManager()
{

}

bool DatabaseManager::initialize()
{
    return connect();
}



bool DatabaseManager::connect()
{

    if(connected_)
    {
        return true;
    }


    /*
        TODO:
        初始化数据库连接

        例如：
        - 创建数据库句柄
        - 设置连接参数
        - 打开数据库文件
        - 建立mysql连接等

    */


    connected_ = true;


    return true;

}



void DatabaseManager::disconnect()
{

    if(!connected_)
    {
        return;
    }


    /*
        TODO:
        释放数据库资源

        例如：
        - 关闭连接
        - 释放句柄

    */


    connected_ = false;

}



bool DatabaseManager::isConnected() const
{

    return connected_;

}