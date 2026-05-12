#ifndef SENSOR_REAL_TIME_DATA_H
#define SENSOR_REAL_TIME_DATA_H

class SensorRealTimeData {
    public:
        SensorRealTimeData();
        ~SensorRealTimeData();

    private:
        
        float _temperatureC;
        float _humidityPct;
};

#endif