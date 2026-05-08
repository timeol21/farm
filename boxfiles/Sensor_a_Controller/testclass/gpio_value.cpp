#include <iostream>            //尽量只用于3588，用来查看对应的gpio口默认是0还是1
#include <fstream>
#include <string>
#include <unistd.h>

using namespace std;

// ====================== 你只需要修改这里 ======================
#define TARGET_GPIO  107      // 可选GPIO: 96(IO1),107(IO2),106(IO3),62(IO4)
#define GPIO_DIR     "out"   // 方向: in(输入) 或 out(输出)
// ==============================================================

#define GPIO_PATH "/sys/class/gpio"

// 自动导出GPIO（程序启动自动执行）
bool gpio_export(int pin) {
    string path = string(GPIO_PATH) + "/gpio" + to_string(pin);
    if (ifstream(path.c_str()).good()) {
        return true;
    }

    ofstream ofs(GPIO_PATH "/export");
    if (!ofs.is_open()) {
        cerr << "错误：导出GPIO失败，请使用 sudo 运行！" << endl;
        return false;
    }
    ofs << pin;
    ofs.close();
    usleep(100000);
    return true;
}

// 设置GPIO方向 in/out
bool gpio_set_direction(int pin, string dir) {
    string path = string(GPIO_PATH) + "/gpio" + to_string(pin) + "/direction";
    ofstream ofs(path.c_str());
    if (!ofs.is_open()) return false;
    ofs << dir;
    ofs.close();
    return true;
}

// 功能1：查询GPIO的value值
int gpio_read_value(int pin) {
    string path = string(GPIO_PATH) + "/gpio" + to_string(pin) + "/value";
    ifstream ifs(path.c_str());
    if (!ifs.is_open()) return -1;
    int val;
    ifs >> val;
    ifs.close();
    return val;
}

// 功能2：改写GPIO的value值（带安全报错：in模式禁止写入）
bool gpio_write_value(int pin, int value) {
    // 检查值必须是 0 或 1
    if (value != 0 && value != 1) {
        cerr << "错误：只能写入 0 或 1！" << endl;
        return false;
    }

    // 读取当前GPIO方向，判断是否为输入模式
    string dir_path = string(GPIO_PATH) + "/gpio" + to_string(pin) + "/direction";
    ifstream dir_file(dir_path.c_str());
    string current_dir;
    if (dir_file.is_open()) {
        dir_file >> current_dir;
        dir_file.close();
    }

    // ===================== 核心报错：in 模式不能写入 =====================
    if (current_dir == "in") {
        cerr << endl << "⚠️  报错：当前GPIO为 in 输入模式，无法写入 " << value << "！写入无效！" << endl;
        return false;
    }
    // ====================================================================

    // 输出模式才执行真正写入
    string path = string(GPIO_PATH) + "/gpio" + to_string(pin) + "/value";
    ofstream ofs(path.c_str());
    if (!ofs.is_open()) return false;
    ofs << value;
    ofs.close();
    return true;
}

int main() {
    // 自动导出GPIO
    if (!gpio_export(TARGET_GPIO)) {
        return -1;
    }

    // 设置方向
    gpio_set_direction(TARGET_GPIO, GPIO_DIR);

    cout << "===== GPIO 控制工具 =====" << endl;
    cout << "当前GPIO: " << TARGET_GPIO << endl;
    cout << "方向: " << GPIO_DIR << endl;
    cout << "========================" << endl;

    // ====================== 功能1：查询值 ======================
    cout << endl << "【1】查询GPIO当前值" << endl;
    int current_val = gpio_read_value(TARGET_GPIO);
    cout << "查询结果: " << current_val << endl;

    // ====================== 功能2：改写值 ======================
    cout << endl << "【2】改写GPIO值" << endl;
    int new_value = 1;          // 在这里修改要写入的值：0 或 1

    // 执行写入（in模式会报错，但不影响后续读取）
    gpio_write_value(TARGET_GPIO, new_value);
    cout << "尝试写入: " << new_value << endl;

    // 写入后再次查询【真实硬件值】（保留你喜欢的功能）
    int check_val = gpio_read_value(TARGET_GPIO);
    cout << "硬件实际检测值: " << check_val << endl;

    return 0;
}

