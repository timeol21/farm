#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
// 海康SDK核心头文件（请根据实际安装路径调整）
#include "HCNetSDK.h"
// 引入自定义日志头文件
#include "logger.h"

// 核心函数：检查并添加IPC到NVR
bool ensureIpcInNvr(LONG lUserID,const std::string& ipcIp,int port,const std::string& user,const std::string& pwd) {
    NET_DVR_IPPARACFG_V40 ipCfg = {0};
    DWORD dwReturned = 0;

    // 1. 获取当前 NVR IP 通道配置
    if (!NET_DVR_GetDVRConfig(
            lUserID,
            NET_DVR_GET_IPPARACFG_V40,
            0,
            &ipCfg,
            sizeof(ipCfg),
            &dwReturned)) {

        LOG_ERROR("Get IP config failed, err=" + std::to_string(NET_DVR_GetLastError()));
        return false;
    }

    // 2. 判断 IPC 是否已经存在
    for (DWORD i = 0; i < ipCfg.dwDChanNum; ++i) {
        if (ipCfg.struIPDevInfo[i].byEnable == 1) {
            std::string existIp = (char*)ipCfg.struIPDevInfo[i].struIP.sIpV4;
            if (existIp == ipcIp) {
                LOG_INFO("IPC already exists: " + ipcIp);
                return true;
            }
        }
    }

    // 3. 找一个空闲位置
    int freeIndex = -1;
    for (DWORD i = 0; i < ipCfg.dwDChanNum; ++i) {
        if (ipCfg.struIPDevInfo[i].byEnable == 0) {
            freeIndex = i;
            break;
        }
    }

    if (freeIndex == -1) {
        LOG_ERROR("No free IP channel available");
        return false;
    }

    // 4. 填充 IPC 信息
    NET_DVR_IPDEVINFO_V31& dev = ipCfg.struIPDevInfo[freeIndex];
    memset(&dev, 0, sizeof(dev));

    dev.byEnable = 1;
    dev.wDVRPort = port;

    // 安全拷贝字符串（防止缓冲区溢出）
    strncpy((char*)dev.struIP.sIpV4, ipcIp.c_str(), sizeof(dev.struIP.sIpV4) - 1);
    strncpy((char*)dev.sUserName, user.c_str(), sizeof(dev.sUserName) - 1);
    strncpy((char*)dev.sPassword, pwd.c_str(), sizeof(dev.sPassword) - 1);

    // 5. 提交配置到 NVR
    if (!NET_DVR_SetDVRConfig(
            lUserID,
            NET_DVR_SET_IPPARACFG_V40,
            0,
            &ipCfg,
            sizeof(ipCfg))) {

        LOG_ERROR("Set IP config failed, err=" + std::to_string(NET_DVR_GetLastError()));
        return false;
    }

    LOG_INFO("IPC added successfully: " + ipcIp);
    return true;
}

// 主函数：完整流程入口
int main() {
    // av_log_set_level(AV_LOG_QUIET);
    // -------------------------- 步骤1：配置连接参数 --------------------------
    // NVR 连接信息（请替换为你的实际参数）
    const std::string NVR_IP = "192.168.31.102";    // NVR的IP地址
    const int NVR_PORT = 8000;                     // NVR的SDK端口（默认8000）
    const std::string NVR_USER = "admin";          // NVR登录用户名
    const std::string NVR_PWD = "gzh13033676063";  // NVR登录密码

    // 待添加的IPC信息（请替换为你的实际参数）
    const std::string IPC_IP = "192.168.31.208";    // IPC的IP地址
    const int IPC_PORT = 8000;                     // IPC的端口
    const std::string IPC_USER = "krdn";            // IPC登录用户名
    const std::string IPC_PWD = "ph4ctc";           // IPC登录密码

    // 可选：自定义日志文件路径（默认是./log/.log）
    // Logger::getInstance().setLogPath("./hik_nvr_log/nvr_ipc_config.log");

    // -------------------------- 步骤2：初始化海康SDK --------------------------
    // 初始化SDK（必须先调用，否则所有操作失败）
    if (!NET_DVR_Init()) {
        LOG_ERROR("HCNetSDK init failed!");
        return -1;
    }

    // 设置SDK日志（可选，方便调试）
    NET_DVR_SetLogToFile(3, "./hik_sdk_log/", true);

    // -------------------------- 步骤3：登录NVR --------------------------
    NET_DVR_USER_LOGIN_INFO loginInfo = {0};
    NET_DVR_DEVICEINFO_V40 devInfo = {0};

    // 填充NVR登录信息
    strncpy((char*)loginInfo.sDeviceAddress, NVR_IP.c_str(), sizeof(loginInfo.sDeviceAddress) - 1);
    loginInfo.wPort = NVR_PORT;
    strncpy((char*)loginInfo.sUserName, NVR_USER.c_str(), sizeof(loginInfo.sUserName) - 1);
    strncpy((char*)loginInfo.sPassword, NVR_PWD.c_str(), sizeof(loginInfo.sPassword) - 1);
    loginInfo.bUseAsynLogin = false;  // 同步登录

    // 登录NVR，获取登录句柄（核心标识，后续所有操作都需要）
    LONG lUserID = NET_DVR_Login_V40(&loginInfo, &devInfo);
    if (lUserID < 0) {
        LOG_ERROR("Login NVR failed, err=" + std::to_string(NET_DVR_GetLastError()));
        NET_DVR_Cleanup();  // 清理SDK资源
        return -1;
    }
    LOG_INFO("Login NVR successfully: " + NVR_IP);

    // -------------------------- 步骤4：调用核心函数添加IPC --------------------------
    bool addResult = false;
    try {
        addResult = ensureIpcInNvr(lUserID, IPC_IP, IPC_PORT, IPC_USER, IPC_PWD);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception when add IPC: " + std::string(e.what()));
    }

    // -------------------------- 步骤5：释放资源 --------------------------
    // 登出NVR（必须调用，否则会导致NVR连接数耗尽）
    if (lUserID >= 0) {
        NET_DVR_Logout(lUserID);
        LOG_INFO("Logout NVR successfully");
    }

    // 清理SDK资源
    NET_DVR_Cleanup();

    // -------------------------- 步骤6：返回执行结果 --------------------------
    if (addResult) {
        LOG_INFO("Process finished successfully");
        return 0;
    } else {
        LOG_ERROR("Process failed");
        return -1;
    }
}