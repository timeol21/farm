class DeviceService {
public:


    bool openFxSolenoid(const std::string& plcId, int yOctal);
    bool closeFxSolenoid(const std::string& plcId, int yOctal);
    bool readFxRegister(const std::string& plcId, int dNumber, uint16_t& value);

//原
    bool openFxSolenoid(const std::string& plcId, int yOctal);
    bool closeFxSolenoid(const std::string& plcId, int yOctal);
    bool readFxRegister(const std::string& plcId, int dNumber, uint16_t& value);
};