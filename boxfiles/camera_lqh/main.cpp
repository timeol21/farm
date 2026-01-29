#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>

class CameraStreamer {
public:
    CameraStreamer(std::string url, std::string output_prefix) 
        : rtsp_url(url), prefix(output_prefix) {}

    void start() {
        std::string cmd = "ffmpeg -rtsp_transport tcp -i \"" + rtsp_url + 
                          "\" -c:v copy -an -f segment -segment_time 60 -reset_timestamps 1 " + 
                          prefix + "_%03d.mp4";

        std::cout << "Starting stream for: " << prefix << std::endl;
        std::system(cmd.c_str());
    }

private:
    std::string rtsp_url;
    std::string prefix;
};

int main() {
    std::string cam2_url = "rtsp://krdn:ph4ctc@192.168.31.208:554/Streaming/Channels/101";
    CameraStreamer cam(cam2_url, "lqh_record");
    cam.start();
    return 0;
}


/*
g++ main.cpp -o camera_app -lavformat -lavcodec -lavutil
*/