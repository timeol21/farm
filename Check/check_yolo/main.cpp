#include <iostream>
#include <opencv2/opencv.hpp>

#include <onnxruntime_cxx_api.h>


using namespace std;
using namespace cv;



class YoloONNX
{

public:

    YoloONNX(
        const string& model_path
    )
        :
        env(
            ORT_LOGGING_LEVEL_WARNING,
            "yolo"
        )
    {

        Ort::SessionOptions options;

        options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL
        );


        session = new Ort::Session(
            env,
            model_path.c_str(),
            options
        );


        Ort::AllocatorWithDefaultOptions allocator;


        auto input_name =
            session->GetInputNameAllocated(
                0,
                allocator
            );


        input_names.push_back(
            input_name.get()
        );


        auto output_name =
            session->GetOutputNameAllocated(
                0,
                allocator
            );


        output_names.push_back(
            output_name.get()
        );


        cout<<"model load success"<<endl;

    }



    vector<float> infer(
        Mat& image
    )
    {

        Mat blob;


        resize(
            image,
            blob,
            Size(640,640)
        );


        blob.convertTo(
            blob,
            CV_32F,
            1.0/255
        );


        vector<float> input_tensor;


        for(int c=0;c<3;c++)
        {
            for(int y=0;y<640;y++)
            {
                for(int x=0;x<640;x++)
                {

                    input_tensor.push_back(
                        blob.at<Vec3f>(y,x)[c]
                    );

                }
            }
        }



        vector<int64_t> input_shape =
        {
            1,
            3,
            640,
            640
        };



        Ort::MemoryInfo memory_info =
            Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator,
                OrtMemTypeDefault
            );



        Ort::Value input_tensor_ort =
            Ort::Value::CreateTensor<float>(
                memory_info,
                input_tensor.data(),
                input_tensor.size(),
                input_shape.data(),
                input_shape.size()
            );



        auto output =
            session->Run(
                Ort::RunOptions{nullptr},
                input_names.data(),
                &input_tensor_ort,
                1,
                output_names.data(),
                1
            );



        float* data =
            output[0]
            .GetTensorMutableData<float>();


        auto shape =
            output[0]
            .GetTensorTypeAndShapeInfo()
            .GetShape();



        int size=1;

        for(auto s:shape)
            size*=s;


        vector<float> result(
            data,
            data+size
        );


        cout<<"output size:"
            <<size
            <<endl;


        return result;

    }



private:


    Ort::Env env;

    Ort::Session* session;


    vector<const char*> input_names;

    vector<const char*> output_names;

};



int main()
{


    string model =
        "./best.onnx";


    string img =
        "./test.jpg";



    YoloONNX yolo(model);



    Mat image =
        imread(img);



    if(image.empty())
    {
        cout<<"image error"<<endl;
        return -1;
    }



    auto output =
        yolo.infer(image);



    cout<<"infer finish"<<endl;



    return 0;
}