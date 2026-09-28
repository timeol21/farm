// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "yolov5.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"

#if defined(RV1106_1103) 
    #include "dma_alloc.hpp"
#endif

#define OUT_PHOTO_DIR       "/home/ztl/rknn_zoo_base/out_photo/"
#define OLD_PHOTO_DIR       "/home/ztl/rknn_zoo_base/examples/yolov5/old_photo/"


// 创建目录，不存在则创建
static int make_dir_if_not_exist(const char *dir_path)
{
    struct stat st;
    if (stat(dir_path, &st) == 0)
    {
        if(S_ISDIR(st.st_mode))
            return 0;
    }
    if(mkdir(dir_path, 0755) == 0)
    {
        return 0;
    }
    printf("mkdir failed: %s\n", dir_path);
    return -1;
}

// 获取不带后缀的文件名
static void get_file_name_no_suffix(const char *full_name, char *out_buf, int buf_size)
{
    const char *p = strrchr(full_name, '/');
    const char *name = p ? (p+1) : full_name;

    const char *dot = strrchr(name, '.');
    int len;
    if(dot)
    {
        len = dot - name;
    }
    else
    {
        len = strlen(name);
    }
    if(len >= buf_size) len = buf_size -1;
    strncpy(out_buf, name, len);
    out_buf[len] = '\0';
}

static int find_first_jpg_in_dir(const char *search_dir, char *out_img_fullpath, int out_buf_len, char *out_filename, int filename_buf_len)
{
    DIR *dir = opendir(search_dir);
    if (!dir)
    {
        printf("open dir %s failed\n", search_dir);
        return -1;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if(strstr(entry->d_name, ".jpg") || strstr(entry->d_name, ".jpeg"))
        {
            // 拼接相对路径 ./model/xxx.jpg
            snprintf(out_img_fullpath, out_buf_len, "%s%s", search_dir, entry->d_name);
            strncpy(out_filename, entry->d_name, filename_buf_len-1);
            out_filename[filename_buf_len-1] = '\0';
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1;
}

/*-------------------------------------------
                  Main Function
-------------------------------------------*/
int main(int argc, char **argv)
{
    /*
    if (argc != 3)
    {
        printf("%s <model_path> <image_path>\n", argv[0]);
        return -1;
    }

    const char *model_path = argv[1];
    const char *image_path = argv[2];
    */
    
    char image_full_path[512] = {0};
    char image_filename[256] = {0};
    char img_name_no_suffix[256]={0};
    char output_png_path[512]={0};
    char dest_move_path[512]={0};
    int write_ret;

    const char *model_path = "./model/yolov5_fp.rknn";
    const char *search_image_dir = "./model/";

    // 1.查找model下第一张jpg
    if(find_first_jpg_in_dir(search_image_dir, image_full_path, sizeof(image_full_path), image_filename, sizeof(image_filename)) !=0)
    {   
        printf("ERROR: ./model 目录没有找到jpg图片，程序直接退出\n");
        return -1;
    }
    printf("found image: %s\n", image_full_path);

    // 2.创建输出目录、old_photo目录
    if(make_dir_if_not_exist(OUT_PHOTO_DIR) !=0) goto out;
    if(make_dir_if_not_exist(OLD_PHOTO_DIR) !=0) goto out;

    // 提取不带后缀名字，拼接输出png路径
    get_file_name_no_suffix(image_filename, img_name_no_suffix, sizeof(img_name_no_suffix));
    snprintf(output_png_path, sizeof(output_png_path), "%s%s.png", OUT_PHOTO_DIR, img_name_no_suffix);
    printf("output png : %s\n", output_png_path);
    
    int ret;
    rknn_app_context_t rknn_app_ctx;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    init_post_process();

    ret = init_yolov5_model(model_path, &rknn_app_ctx);
    if (ret != 0)
    {
        printf("init_yolov5_model fail! ret=%d model_path=%s\n", ret, model_path);
        goto out;
    }
    

    image_buffer_t src_image;
    memset(&src_image, 0, sizeof(image_buffer_t));
    ret = read_image(image_full_path, &src_image);

#if defined(RV1106_1103) 
    //RV1106 rga requires that input and output bufs are memory allocated by dma
    ret = dma_buf_alloc(RV1106_CMA_HEAP_PATH, src_image.size, &rknn_app_ctx.img_dma_buf.dma_buf_fd, 
                       (void **) & (rknn_app_ctx.img_dma_buf.dma_buf_virt_addr));
    memcpy(rknn_app_ctx.img_dma_buf.dma_buf_virt_addr, src_image.virt_addr, src_image.size);
    dma_sync_cpu_to_device(rknn_app_ctx.img_dma_buf.dma_buf_fd);
    free(src_image.virt_addr);
    src_image.virt_addr = (unsigned char *)rknn_app_ctx.img_dma_buf.dma_buf_virt_addr;
    src_image.fd = rknn_app_ctx.img_dma_buf.dma_buf_fd;
    rknn_app_ctx.img_dma_buf.size = src_image.size;
#endif

    if (ret != 0)
    {
        printf("read image fail! ret=%d image_path=%s\n", ret, image_full_path);
    }

    object_detect_result_list od_results;

    ret = inference_yolov5_model(&rknn_app_ctx, &src_image, &od_results);
    if (ret != 0)
    {
        printf("init_yolov5_model fail! ret=%d\n", ret);
        goto out;
    }

    // 画框和概率
    char text[256];
    for (int i = 0; i < od_results.count; i++)
    {
        object_detect_result *det_result = &(od_results.results[i]);
        printf("%s @ (%d %d %d %d) %.3f\n", coco_cls_to_name(det_result->cls_id),
               det_result->box.left, det_result->box.top,
               det_result->box.right, det_result->box.bottom,
               det_result->prop);
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;

        draw_rectangle(&src_image, x1, y1, x2 - x1, y2 - y1, COLOR_BLUE, 3);

        sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
        draw_text(&src_image, text, x1, y1 - 20, COLOR_RED, 10);
    }

    // write_image("out.png", &src_image);
    
    // 保存画框后的png图片，调用write_image，使用output_png_path路径
    write_ret = write_image(output_png_path, &src_image);
    printf("[INFO] write_image ret=%d , output png path: %s\n", write_ret, output_png_path);

    if(write_ret != -1)
    {
        printf("save result image success: %s , write_ret=%d\n", output_png_path, write_ret);
    
        char dest_jpg[512] = {0};
        snprintf(dest_jpg, sizeof(dest_jpg), "%s%s", OLD_PHOTO_DIR, image_filename);
        printf("[DEBUG] move src=%s --> dst=%s\n", image_full_path, dest_jpg);
    
        int rc = rename(image_full_path, dest_jpg);
        if(rc == 0)
        {
            printf("==== original image move SUCCESS ====\n");
        }
        else
        {
            perror("rename fail");
        }
    }
    else
    {
        printf("WARN: write_image failed !!! skip move original file\n");
    }

out:
    deinit_post_process();
    
// 只有模型上下文有效才执行释放，避免释放未初始化对象
    if(rknn_app_ctx.rknn_ctx != 0)
    {
        ret = release_yolov5_model(&rknn_app_ctx);
        if (ret != 0)
        {
            printf("release_yolov5_model fail! ret=%d\n", ret);
        }
    }

    if (src_image.virt_addr != NULL)
    {
#if defined(RV1106_1103) 
        dma_buf_free(rknn_app_ctx.img_dma_buf.size, &rknn_app_ctx.img_dma_buf.dma_buf_fd, 
                rknn_app_ctx.img_dma_buf.dma_buf_virt_addr);
#else
        free(src_image.virt_addr);
#endif
    }

    return 0;
}
