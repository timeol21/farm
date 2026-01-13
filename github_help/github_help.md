
打开VPN以后执行：（检查是否能连接上网络）

ztl@RK356X:~/program$ ping www.baidu.com -c 3
PING www.a.shifen.com (180.101.49.44) 56(84) bytes of data.
64 字节，来自 180.101.49.44 (180.101.49.44): icmp_seq=1 ttl=50 时间=16.3 毫秒
64 字节，来自 180.101.49.44 (180.101.49.44): icmp_seq=2 ttl=50 时间=17.4 毫秒
64 字节，来自 180.101.49.44 (180.101.49.44): icmp_seq=3 ttl=50 时间=17.3 毫秒

--- www.a.shifen.com ping 统计 ---
已发送 3 个包， 已接收 3 个包, 0% 包丢失, 耗时 2003 毫秒
rtt min/avg/max/mdev = 16.258/16.993/17.397/0.520 ms
ztl@RK356X:~/program$ ping github.com -c 3
PING github.com (20.205.243.166) 56(84) bytes of data.
64 字节，来自 20.205.243.166 (20.205.243.166): icmp_seq=1 ttl=108 时间=94.4 毫秒
64 字节，来自 20.205.243.166 (20.205.243.166): icmp_seq=2 ttl=108 时间=93.7 毫秒
64 字节，来自 20.205.243.166 (20.205.243.166): icmp_seq=3 ttl=108 时间=98.6 毫秒

--- github.com ping 统计 ---
已发送 3 个包， 已接收 3 个包, 0% 包丢失, 耗时 2003 毫秒
rtt min/avg/max/mdev = 93.709/95.586/98.615/2.161 ms
ztl@RK356X:~/program$ find /home/ztl -name ".git" -type d(找到可以设置成远程仓库的对应的文件夹)
/home/ztl/program/.git
/home/ztl/rknn_model_zoo/.git
ztl@RK356X:~/program$ # 查看远程仓库关联信息
ztl@RK356X:~/program$ git remote -v
origin  git@github.com:timeol21/farm.git (fetch)
origin  git@github.com:timeol21/farm.git (push)
ztl@RK356X:~/program$ git add .(或者是git add /home/ztl/program/github_help/github_help.md或者是git add program/github_help/github_help.md)
ztl@RK356X:~/program$ git commit -m "具体改动的说明"
位于分支 main
您的分支与上游分支 'origin/main' 一致。

无文件要提交，干净的工作区
ztl@RK356X:~/program$ git pull origin main (或者git pull origin master看你的仓库的主分支的名称)
Enter passphrase for key '/home/ztl/.ssh/id_ed25519': 
来自 github.com:timeol21/farm
 * branch            main       -> FETCH_HEAD
已经是最新的。
ztl@RK356X:~/program$ git push origin main
Enter passphrase for key '/home/ztl/.ssh/id_ed25519': 
Everything up-to-date
ztl@RK356X:~/program$ 


如果出现了合并冲突的情况：

此时的解决步骤（新手友好）：
打开冲突文件（比如 program/boxfiles/control_y0.cpp），会看到类似标记：
cpp
运行
<<<<<<< HEAD  // 你本地的修改
int a = 10;   // 你改的内容
=======       // 远程仓库的修改
int a = 20;   // 网页端改的内容
>>>>>>> origin/main
手动解决冲突：保留你需要的内容，删除冲突标记（<<<<<<</=======/>>>>>>>），比如保留本地的 int a = 10;；
暂存解决后的文件：
bash
运行
git add program/boxfiles/control_y0.cpp
完成合并并推送：
bash
运行
git commit -m "解决冲突：保留本地boxfiles的修改"
git push origin main