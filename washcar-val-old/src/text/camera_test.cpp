#include "data_layer/camera/camera_device.h"
#include "data_layer/camera/camera.h"
#include "data_layer/camera/camera_instance_set.h"
#include "data_layer/camera/camera_status.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace {
// ---- 摄像头列表(改这里就能加/减/改摄像头,不用动 main)----
struct CameraConfig {
    const char* id;
    const char* name;
    const char* rtspUrl;
};
constexpr CameraConfig kCameras[] = {
    {"cam_001", "实验室摄像头(萤石云)",      "rtsp://admin:QBKGWT@192.168.31.108:554/h264/ch1/main/av_stream"},
    //{"cam_002", "实验室摄像头(海康)",        "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101"},
    //{"cam_003", "洗车机前摄像头(萤石云)",      "rtsp://admin:HLSFYW@192.168.0.101:554/h264/ch1/main/av_stream"},
    //{"cam_004", "洗车机后摄像头(萤石云)",      "rtsp://admin:ZDHYOI@192.168.0.102:554/h264/ch1/main/av_stream"},
};

CameraDevice createCameraDevice(const char* id, const char* name, const char* url) {
    return CameraDevice(id, name, url);
}

Camera createCamera(const char* id, const char* name, const char* url) {
    return Camera(createCameraDevice(id, name, url));
}
}

void testCameraDevice(const char* id, const char* name, const char* url) {
    std::cout << "=== Test CameraDevice ===" << std::endl;

    CameraDevice device = createCameraDevice(id, name, url);

    std::cout << "Camera ID: " << device.getCameraId() << std::endl;
    std::cout << "Camera Name: " << device.getName() << std::endl;
    std::cout << "RTSP URL: " << device.getRtspUrl() << std::endl;

    std::cout << "CameraDevice test passed!" << std::endl << std::endl;
}

void testSingleCamera(const char* id, const char* name, const char* url) {
    std::cout << "=== Test Single Camera (" << name << ") ===" << std::endl;

    CameraDevice device = createCameraDevice(id, name, url);
    Camera camera = createCamera(id, name, url);

    std::cout << "Camera ID: " << camera.getCameraId() << std::endl;
    std::cout << "Camera Name: " << device.getName() << std::endl;
    std::cout << "RTSP URL: " << camera.getRtspUrl() << std::endl;
    std::cout << "Is Running: " << (camera.isRunning() ? "true" : "false") << std::endl;

    CameraStatus status = camera.getStatus();
    std::cout << "Initial Status: " << (status.getStatus() == CameraOnlineStatus::ONLINE ? "ONLINE" : "OFFLINE") << std::endl;

    std::cout << "Single camera test passed!" << std::endl << std::endl;
}

void testCameraInstanceSet(const char* id, const char* name, const char* url) {
    std::cout << "=== Test CameraInstanceSet (" << name << ") ===" << std::endl;

    CameraInstanceSet cameraSet;

    CameraDevice device = createCameraDevice(id, name, url);

    cameraSet.addCamera(id, std::make_unique<Camera>(device));

    std::cout << "Added 1 camera to set" << std::endl;

    Camera* cam = cameraSet.getCamera(id);
    if (cam) {
        std::cout << "Found camera: " << cam->getCameraId() << std::endl;
        std::cout << "RTSP URL: " << cam->getRtspUrl() << std::endl;
    } else {
        std::cout << "Camera " << id << " not found!" << std::endl;
    }

    const auto& allCameras = cameraSet.getAll();
    std::cout << "Total cameras in set: " << allCameras.size() << std::endl;

    auto statusList = cameraSet.getCameraStatusList();
    std::cout << "Camera status list size: " << statusList.size() << std::endl;

    std::cout << "CameraInstanceSet test passed!" << std::endl << std::endl;
}

void testCameraLifecycle(const char* id, const char* name, const char* url) {
    std::cout << "=== Test Camera Lifecycle ===" << std::endl;

    Camera camera = createCamera(id, name, url);

    std::cout << "Initial state - Is Running: " << (camera.isRunning() ? "true" : "false") << std::endl;

    camera.start();
    std::cout << "After start - Is Running: " << (camera.isRunning() ? "true" : "false") << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    camera.stop();
    std::cout << "After stop - Is Running: " << (camera.isRunning() ? "true" : "false") << std::endl;

    std::cout << "Camera lifecycle test passed!" << std::endl << std::endl;
}

void testCameraFrameCapture(const char* id, const char* name, const char* url) {
    std::cout << "=== Test Camera Frame Capture (" << name << ") ===" << std::endl;

    Camera camera = createCamera(id, name, url);

    std::cout << "Starting camera..." << std::endl;
    if (!camera.start()) {
        std::cerr << "Failed to start camera!" << std::endl;
        return;
    }

    std::cout << "Waiting for frame..." << std::endl;
    CameraRealTimeFrame frame;
    int waitCount = 0;
    const int maxWait = 100;

    while (!(frame = camera.getRealTimeFrame()).isValid() && waitCount < maxWait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitCount++;
    }

    if (waitCount >= maxWait) {
        std::cerr << "Timeout waiting for frame!" << std::endl;
        camera.stop();
        return;
    }

    std::cout << "Got frame!" << std::endl;
    std::cout << "Frame dimensions: " << frame.getWidth() << "x" << frame.getHeight() << std::endl;
    std::cout << "Frame timestamp: " << frame.getTimestamp() << std::endl;

    std::string outputFile = "test_capture_" + std::string(id) + "_" + std::to_string(frame.getTimestamp()) + ".bmp";
    if (frame.saveAsBmp(outputFile)) {
        std::cout << "Successfully saved frame to: " << outputFile << std::endl;
    } else {
        std::cerr << "Failed to save frame!" << std::endl;
    }

    camera.stop();
    std::cout << "Camera frame capture test passed!" << std::endl << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  Camera Module Test (多摄像头)         " << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    try {
        // 对每个摄像头串行跑全 5 个测试;加摄像头只需改 kCameras 列表
        for (const auto& cam : kCameras) {
            std::cout << "=== " << cam.name << " (" << cam.id << ") ===" << std::endl;
            std::cout << "  RTSP: " << cam.rtspUrl << std::endl << std::endl;

            testCameraDevice(cam.id, cam.name, cam.rtspUrl);
            testSingleCamera(cam.id, cam.name, cam.rtspUrl);
            testCameraInstanceSet(cam.id, cam.name, cam.rtspUrl);
            testCameraLifecycle(cam.id, cam.name, cam.rtspUrl);
            testCameraFrameCapture(cam.id, cam.name, cam.rtspUrl);
        }

        std::cout << "========================================" << std::endl;
        std::cout << "     All Camera Tests Passed! ✓        " << std::endl;
        std::cout << "========================================" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
