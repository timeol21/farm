#include <QApplication>
#include <QLabel>
#include <QWidget>
#include <QObject>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QWidget w;
    w.setWindowTitle("Qt5.12.8 HDMI页面测试");

    // ========== 外层水平布局：左边面板 | 右边图片区 ==========
    QHBoxLayout* mainHLayout = new QHBoxLayout(&w);

    // ========== 左侧垂直布局：统计信息 + 异常按钮列表 ==========
    QWidget *leftWidget = new QWidget();
    QVBoxLayout *leftVLayout = new QVBoxLayout(leftWidget);

    // 工件统计标签
    int totalWorkpiece = 120;    // 今日总工件
    int errorCount = 8;          // 异常数量
    double successRate = (totalWorkpiece - errorCount) / (double)totalWorkpiece * 100.0;

    QLabel *statLabel = new QLabel(QString("今日工件总数：%1\n异常数量：%2\n成功率：%3%")
                                       .arg(totalWorkpiece)
                                       .arg(errorCount)
                                       .arg(successRate, 0, 'f', 2));
    leftVLayout->addWidget(statLabel);

    // 图片文件名列表，对应3个异常
    QStringList imgFileList = {"test.jpg", "test1.jpg", "test2.jpg"};
    QLabel *imgLabel = new QLabel; // 右侧图片显示控件

    // 循环创建异常按钮
    for(int i=0; i<imgFileList.size(); i++)
    {
        QPushButton *btn = new QPushButton(QString("异常%1").arg(i+1));
        leftVLayout->addWidget(btn);

        // Lambda绑定点击事件，切换图片
        QObject::connect(btn, &QPushButton::clicked, [=](){
            QPixmap pix(imgFileList[i]);
            if(pix.isNull())
            {
                imgLabel->setText(QString("❌ %1 加载失败！").arg(imgFileList[i]));
                imgLabel->setPixmap(QPixmap());
            }
            else
            {
                QPixmap pixScaled = pix.scaled(600, 338, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                imgLabel->setPixmap(pixScaled);
            }
        });
    }
    leftVLayout->addStretch(); // 底部填充空白，按钮靠上排列

    // ========== 右侧图片区域 ==========
    // 先加载默认第一张图片
    QPixmap initPix(imgFileList[0]);
    if(initPix.isNull())
    {
        imgLabel->setText("❌ test.jpg 图片加载失败！检查文件名/权限");
    }
    else
    {
        QPixmap pixScaled = initPix.scaled(600, 338, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imgLabel->setPixmap(pixScaled);
    }

    // ========== 左右两个widget加到外层水平布局 ==========
    mainHLayout->addWidget(leftWidget);
    mainHLayout->addWidget(imgLabel);

    // ========== SQLite数据库测试（保留你原有代码） ==========
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("test.db");
    bool ok = db.open();
    QLabel* dbLabel;
    if(ok){
        dbLabel = new QLabel("✅ SQLite数据库打开成功");
    }else{
        dbLabel = new QLabel("❌ SQLite失败："+db.lastError().text());
    }
    // 数据库提示放到左侧面板最底部
    leftVLayout->addWidget(dbLabel);

    w.resize(1100,640); // 窗口调大，适配左右分栏
    w.show();
    return app.exec();
}
