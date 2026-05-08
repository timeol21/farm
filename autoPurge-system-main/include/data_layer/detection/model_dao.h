#pragma once


class IModelDao{
public:
    virtual ~IModelDao() = default;



};



class ModelDao: public IModelDao{ 
public:
    ModelDao() ;
    ~ModelDao();




};