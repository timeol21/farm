// #include <iostream>
// #include <modbus/modbus.h>

// int main() {
//     modbus_t *ctx = modbus_new_rtu("/dev/ttyS4", 9600, 'N', 8, 1);
//     if (!ctx) return -1;

//     modbus_set_slave(ctx, 1);

//     if (modbus_connect(ctx) == -1) {
//         std::cerr << "PLC 连接失败\n";  // 修复乱码：PLC连接失败
//         modbus_free(ctx);
//         return -1;
//     }

//     uint8_t x0;
//     int rc = modbus_read_input_bits(ctx, 1024, 1, &x0);

//     if (rc == -1) {
//         std::cerr << "读取 X0 失败\n";  // 修复乱码：读取X0失败
//     } else {
//         std::cout << "X0 = " << (int)x0 << std::endl;

//         if (x0 == 1)
//             std::cout << "有 触发信号输入\n";  // 修复乱码：有触发信号输入
//         else
//             std::cout << "无 触发信号输入\n";  // 修复乱码：无触发信号输入
//     }

//     modbus_close(ctx);
//     modbus_free(ctx);
//     return 0;
// }

// #include <iostream>
// #include <modbus/modbus.h>

// int main() {
//     // 创建RTU上下文：串口、波特率、校验位、数据位、停止位
//     modbus_t *ctx = modbus_new_rtu("/dev/ttyS4", 9600, 'N', 8, 1);
//     if (!ctx) return -1;

//     // 设置从站地址（PLC地址）
//     modbus_set_slave(ctx, 1);

//     // 连接PLC
//     if (modbus_connect(ctx) == -1) {
//         std::cerr << "PLC connection failed\n";
//         modbus_free(ctx);
//         return -1;
//     }

//     // 读取输入位X0（地址1024）
//     uint8_t x0;
//     int rc = modbus_read_input_bits(ctx, 1024, 1, &x0);

//     // 处理读取结果
//     if (rc == -1) {
//         std::cerr << "Failed to read X0\n";
//     } else {
//         std::cout << "X0 = " << (int)x0 << std::endl;

//         if (x0 == 1)
//             std::cout << "Trigger signal detected\n";  // 有触发信号
//         else
//             std::cout << "No trigger signal\n";        // 无触发信号
//     }

//     // 释放资源
//     modbus_close(ctx);
//     modbus_free(ctx);
//     return 0;
// }

// #include <iostream>
// #include <modbus/modbus.h>

// int main() {
//     modbus_t *ctx = modbus_new_rtu("/dev/ttyS4", 9600, 'N', 8, 1);
//     if (!ctx) return -1;

//     modbus_set_slave(ctx, 1);

//     if (modbus_connect(ctx) == -1) {
//         std::cerr << "PLC ������\n";
//         modbus_free(ctx);
//         return -1;
//     }

//     uint8_t x0;
//     int rc = modbus_read_input_bits(ctx, 1024, 1, &x0);

//     if (rc == -1) {
//         std::cerr << "��ȡ X0 ʧ��\n";
//     } else {
//         std::cout << "X0 = " << (int)x0 << std::endl;

//         if (x0 == 1)
//             std::cout << "�� ���ź�����\n";
//         else
//             std::cout << "�� �������ź�\n";
//     }

//     modbus_close(ctx);
//     modbus_free(ctx);
//     return 0;
// }
