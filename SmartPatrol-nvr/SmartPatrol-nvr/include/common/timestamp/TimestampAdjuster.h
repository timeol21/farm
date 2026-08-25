#ifndef TIME_STAMP_ADJUSTER_H
#define TIME_STAMP_ADJUSTER_H
extern "C" {
#include <libavcodec/avcodec.h>
}

class TimestampAdjuster {
public:
    TimestampAdjuster()
        : basePts_(AV_NOPTS_VALUE), baseDts_(AV_NOPTS_VALUE) {}
    void reset(){
        basePts_ = AV_NOPTS_VALUE;
        baseDts_ = AV_NOPTS_VALUE;
    }
    void adjust(AVPacket* pkt){
        if (!pkt) return;

        if (basePts_ == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
            basePts_ = pkt->pts;
        if (baseDts_ == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
            baseDts_ = pkt->dts;

        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts -= basePts_;
        if (pkt->dts != AV_NOPTS_VALUE)
            pkt->dts -= baseDts_;

        if (pkt->pts < 0) pkt->pts = 0;
        if (pkt->dts < 0) pkt->dts = 0;
    }

private:
    int64_t basePts_;
    int64_t baseDts_;
};

#endif