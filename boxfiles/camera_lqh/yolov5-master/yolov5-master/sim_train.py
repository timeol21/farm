import random
import time
import numpy as np
from datetime import datetime

# ===================== 配置项 =====================
TOTAL_EPOCH_PER_RUN = 3000   # 每一轮训练 3000 epoch
GPU_MEM_BASE = 4.2           # 模拟GPU显存基数
CLASSES = 80                 # coco128默认80类
BATCH_NUM = 8                # 模拟每个epoch有8个batch，可改
SEED_BASE = 0
# ==================================================

def generate_loss(epoch, total_epoch):
    """模拟loss随epoch下降，带随机抖动"""
    # 衰减曲线
    decay = np.exp(-epoch / (total_epoch * 0.35))
    box_loss = 0.08 * decay + random.uniform(-0.002, 0.002)
    obj_loss = 0.06 * decay + random.uniform(-0.002, 0.002)
    cls_loss = 0.04 * decay + random.uniform(-0.001, 0.001)
    box_loss = max(0.001, box_loss)
    obj_loss = max(0.001, obj_loss)
    cls_loss = max(0.001, cls_loss)
    return round(box_loss,4), round(obj_loss,4), round(cls_loss,4)

def generate_metrics(epoch, total_epoch):
    """模拟mAP、precision、recall逐步上升"""
    progress = epoch / total_epoch
    p = 0.25 + 0.68 * (1 - np.exp(-progress * 3.2)) + random.uniform(-0.015,0.015)
    r = 0.22 + 0.71 * (1 - np.exp(-progress * 3.0)) + random.uniform(-0.015,0.015)
    m5 = 0.20 + 0.74 * (1 - np.exp(-progress * 2.8)) + random.uniform(-0.015,0.015)
    m595 = 0.08 + 0.52 * (1 - np.exp(-progress * 2.6)) + random.uniform(-0.012,0.012)
    p = np.clip(p,0,1)
    r = np.clip(r,0,1)
    m5 = np.clip(m5,0,1)
    m595 = np.clip(m595,0,1)
    val_box = 0.07 * np.exp(-progress*2.5) + random.uniform(-0.002,0.002)
    val_obj = 0.05 * np.exp(-progress*2.5) + random.uniform(-0.002,0.002)
    val_cls = 0.03 * np.exp(-progress*2.5) + random.uniform(-0.001,0.001)
    return round(p,4),round(r,4),round(m5,4),round(m595,4),round(val_box,4),round(val_obj,4),round(val_cls,4)


def single_train_run(run_idx):
    """单次完整训练流程：3000 epoch，打印完整YOLOv5 train日志"""
    print(f"\n{'='*70}")
    print(f"========== START NEW TRAIN RUN #{run_idx} | {datetime.now()} ==========")
    print(f"{'='*70}\n")
    # ===== 1. 开头参数打印（和原版train.py print_args一致）=====
    print("Namespace(")
    print("  weights='yolov5s.pt', cfg='', data='data/coco128.yaml', hyp='data/hyps/hyp.scratch-low.yaml',")
    print("  epochs=3000, batch_size=16, imgsz=640, rect=False, resume=False, nosave=False, noval=False,")
    print("  noautoanchor=False, noplots=False, evolve=None, bucket='', cache=None, image_weights=False,")
    print("  device='', multi_scale=False, single_cls=False, optimizer='SGD', sync_bn=False, workers=8,")
    print("  project='runs/train', name='exp', exist_ok=False, quad=False, cos_lr=False, label_smoothing=0.0,")
    print("  patience=100, freeze=[0], save_period=-1, seed=0, local_rank=-1, entity=None, upload_dataset=False,")
    print("  bbox_interval=-1, artifact_alias='latest'")
    print(")")
    print("hyperparameters: lr0=0.01, lrf=0.01, momentum=0.937, weight_decay=0.0005, warmup_epochs=3.0, warmup_momentum=0.8, warmup_bias_lr=0.1, box=0.05, cls=0.5, cls_pw=1.0, obj=1.0, obj_pw=1.0, iou_t=0.2, anchor_t=4.0, hsv_h=0.015, hsv_s=0.7, hsv_v=0.4, degrees=0.0, translate=0.1, scale=0.5, shear=0.0, perspective=0.0, flipud=0.0, fliplr=0.5, mosaic=1.0, mixup=0.0, copy_paste=0.0")
    print("Transferred 349/349 items from yolov5s.pt")
    print("Image sizes 640 train, 640 val")
    print("Using 8 dataloader workers")
    print("Logging results to runs/train/exp")
    print("Starting training for 3000 epochs...\n")

    # 表头
    header = ("%11s" *7) % ('Epoch', 'GPU_mem', 'box_loss', 'obj_loss', 'cls_loss', 'Instances', 'Size')
    print(header)

    best_fitness = 0.0
    start_time = time.time()

    for epoch in range(TOTAL_EPOCH_PER_RUN):
        # 模拟显存缓慢波动
        gpu_mem = round(GPU_MEM_BASE + random.uniform(-0.3, 0.8),3)
        box_loss, obj_loss, cls_loss = generate_loss(epoch, TOTAL_EPOCH_PER_RUN)

        # tqdm进度条模拟，BATCH_NUM个batch
        for batch_i in range(BATCH_NUM):
            instances = random.randint(12,64)
            line = ('%11s'*2 + '%11.4g'*5) % (
                f"{epoch}/{TOTAL_EPOCH_PER_RUN-1}",
                f"{gpu_mem}G",
                box_loss, obj_loss, cls_loss,
                instances,
                640
            )
            print("\r" + line, end="", flush=True)
            time.sleep(0.005) # 控制打印速度，调大可以变慢，调小更快
        print("") # 换行，结束这个epoch进度条

        # epoch结束，执行val，输出mAP指标
        p, r, m5, m595, v_box, v_obj, v_cls = generate_metrics(epoch, TOTAL_EPOCH_PER_RUN)
        fit = 0.1*m5 + 0.9*m595
        if fit > best_fitness:
            best_fitness = fit
            print(f"      Epoch {epoch} new best fitness={fit:.4f} saving best.pt")
        print(f"                 all        100      {p:.4f}      {r:.4f}      {m5:.4f}      {m595:.4f}")
        print(f"val: box_loss={v_box:.4f}, obj_loss={v_obj:.4f}, cls_loss={v_cls:.4f}")
        print(f"Saving last.pt, best.pt")

    # 一轮全部3000epoch跑完，收尾打印
    total_h = (time.time() - start_time)/3600
    print(f"\n3000 epochs completed in {total_h:.3f} hours.")
    print("Optimizer stripped from runs/train/exp/weights/last.pt")
    print("Optimizer stripped from runs/train/exp/weights/best.pt")
    print(f"\nValidating runs/train/exp/weights/best.pt...")
    print(f"                 all        100      {p:.4f}      {r:.4f}      {m5:.4f}      {m595:.4f}")
    print(f"Results saved to runs/train/exp")
    print(f"========== FINISH TRAIN RUN #{run_idx} ==========\n")


if __name__ == "__main__":
    run_counter = 1
    while True:
        single_train_run(run_counter)
        run_counter += 1
        print(f"\n===== RUN {run_counter-1} FINISHED, WILL START NEXT 3000 EPOCH TRAINING =====")
        time.sleep(1)
