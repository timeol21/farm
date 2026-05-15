#include "Solenoid.h"

int main() {
    Solenoid solenoid(1, "id", "on", "ttyS4", 1, 1, "solenoid1", 1);
    // solenoid.InitSerial("/dev/ttyS4");
    // solenoid.ConfigSerial();
    // solenoid.OpenSolenoid();
    // solenoid.CloseSolenoid();
    // solenoid.QuerySolenoid();
    return 0;
}
