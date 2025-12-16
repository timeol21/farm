#include <iostream>
#include <modbus/modbus.h>

int main() {
    modbus_t *ctx = modbus_new_rtu("/dev/ttyS4", 9600, 'N', 8, 1);
    if (!ctx) return -1;

    modbus_set_slave(ctx, 1);

    if (modbus_connect(ctx) == -1) {
        std::cerr << "PLC 不在线\n";
        modbus_free(ctx);
        return -1;
    }

    uint8_t x0;
    int rc = modbus_read_input_bits(ctx, 1024, 1, &x0);

    if (rc == -1) {
        std::cerr << "读取 X0 失败\n";
    } else {
        std::cout << "X0 = " << (int)x0 << std::endl;

        if (x0 == 1)
            std::cout << "→ 有信号输入\n";
        else
            std::cout << "→ 无输入信号\n";
    }

    modbus_close(ctx);
    modbus_free(ctx);
    return 0;
}
