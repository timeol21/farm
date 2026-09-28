# 清理旧编译产物（推荐，避免缓存问题）
make clean
qmake
make
export DISPLAY=:0
./test

cp /home/ztl/program/Check/check_qt/* /home/ztl/program/Check/check_qt_base/

