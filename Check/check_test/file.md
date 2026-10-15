这是一个很小的，可以进行yolo识别的系统


ztl@RK356X:~/program/Check/check_test$ tree -I "build"
.
├── 3rdparty
│   ├── CMakeLists.txt
│   ├── jpeg_turbo
│   │   ├── include
│   │   │   └── turbojpeg.h
│   │   └── Linux
│   │       └── aarch64
│   │           └── libturbojpeg.a
│   ├── librga
│   │   ├── include
│   │   │   ├── drmrga.h
│   │   │   ├── im2d_buffer.h
│   │   │   ├── im2d_common.h
│   │   │   ├── im2d.h
│   │   │   ├── im2d_mpi.h
│   │   │   ├── im2d_single.h
│   │   │   ├── im2d_task.h
│   │   │   ├── im2d_type.h
│   │   │   ├── im2d_version.h
│   │   │   └── rga.h
│   │   └── Linux
│   │       └── aarch64
│   │           ├── librga.a
│   │           └── librga.so
│   ├── rknpu2
│   │   ├── include
│   │   │   └── rknn_api.h
│   │   └── Linux
│   │       └── aarch64
│   │           └── librknnrt.so
│   └── stb_image
│       ├── stb_image.h
│       └── stb_image_write.h
├── build-linux.sh
├── examples
│   └── yolov5
│       ├── cpp
│       │   ├── CMakeLists.txt
│       │   ├── main.cc
│       │   ├── postprocess.cc
│       │   ├── postprocess.h
│       │   ├── rknpu2
│       │   │   ├── yolov5.cc
│       │   │   └── yolov5_rv1106_1103.cc
│       │   ├── tree.md
│       │   └── yolov5.h
│       ├── model
│       │   ├── 20260914_132850_145.jpg
│       │   ├── anchors_yolov5.txt
│       │   ├── coco_80_labels_list.txt
│       │   ├── download_model.sh
│       │   └── yolov5_fp.rknn
│       └── old_photo
│           └── 20260914_132850_145.jpg
├── file.md
├── install
│   └── rk356x_linux_aarch64
│       └── rknn_yolov5_demo
│           ├── lib
│           │   ├── librga.so
│           │   └── librknnrt.so
│           ├── model
│           │   ├── coco_80_labels_list.txt
│           │   └── yolov5_fp.rknn
│           └── rknn_yolov5_demo
├── out_photo
│   └── 20260914_132850_145.png
└── utils
    ├── CMakeLists.txt
    ├── common.h
    ├── file_utils.c
    ├── file_utils.h
    ├── font.h
    ├── image_drawing.c
    ├── image_drawing.h
    ├── image_utils.c
    └── image_utils.h

27 directories, 50 files
ztl@RK356X:~/program/Check/check_test$

其中install是用./build-linux.sh脚本编译出来的