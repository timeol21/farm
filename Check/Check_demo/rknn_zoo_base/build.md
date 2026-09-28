然后将对应的.rknn下载到rk3568板端
来到
cd /home/ztl/rknn_model_zoo/

如果是自己训练的模型，需要修改
/home/ztl/rknn_model_zoo/examples/yolov5/cpp/postprocess.h中的OBJ_CLASS_NUM改为自己的class种类数量
还有/home/ztl/rknn_model_zoo/examples/yolov5/model/coco_80_labels_list.txt文件中放自己的标签名字
（tips：找不到文件可以用grep -rl "OBJ_CLASS_NUM" /home/ztl/rknn_model_zoo/来找到对应的目录下包含这个词的所有文件）

然后执行在
ztl@RK356X:~/rknn_model_zoo$下
#chmod +x build-linux.sh 将这个脚本加上可执行权限
./build-linux.sh -t rk356x -a aarch64 -d yolov5
或者是./build-linux.sh -t rk356x -a aarch64 -d yolov5 -r 不开rga加速
（这里3568写作356x，其他板子正常写）

来到
/home/ztl/rknn_model_zoo/install/rk356x_linux_aarch64/rknn_yolov5_demo/model/这个目录下，放转化好的.rknn模型
如果是自己训练的就在这个路径在放好自己要预测的一张图片
/home/ztl/rknn_model_zoo/install/rk356x_linux_aarch64/rknn_yolov5_demo/model/

后再/home/ztl/rknn_model_zoo/install/rk356x_linux_aarch64/rknn_yolov5_demo/下执行：
./rknn_yolov5_demo model/yolov5_fp.rknn model/20260914_143133_376.jpg


然后/home/ztl/rknn_model_zoo/install/rk356x_linux_aarch64/rknn_yolov5_demo/下面就有一张out.png的预测图片