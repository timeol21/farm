#pragma once


class Plc
{

public:


    virtual ~Plc() = default;



public:


    virtual bool initialize() = 0;



    virtual bool start() = 0;



    virtual void stop() = 0;


};