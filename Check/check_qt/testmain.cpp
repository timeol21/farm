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
#include <QStringList>
#include <QPixmap>

#include "defectwindow.h"

//图片辅助类实现
QPixmap ImageDisplayHelper::loadAndScaleImage(const QString& filePath, int targetW, int targetH)
{
    QPixmap pix(filePath);
    if (pix.isNull())
    {
        qDebug() << "图片加载失败:" << filePath;
        return QPixmap();
    }
    QPixmap scaledPix = pix.scaled(targetW, targetH,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    return scaledPix;
}

//数据库工具类实现
SqliteDbHelper::SqliteDbHelper()
{
    db = QSqlDatabase::addDatabase("QSQLITE");
}

SqliteDbHelper::~SqliteDbHelper()
{
    if(db.isOpen())
        db.close();
}

bool SqliteDbHelper::openDb(const QString& dbFileName)
{
    db.setDatabaseName(dbFileName);
    return db.open();
}

QString SqliteDbHelper::getErrorMsg() const
{
    return db.lastError().text();
}

//主窗口构造函数实现
DefectMainWindow::DefectMainWindow(QWidget *parent)
    : QWidget(parent)
{
    this->setWindowTitle("Qt5.12.8 HDMI页面测试");
    this->resize(960, 540);

    QHBoxLayout* mainHLayout = new QHBoxLayout(this);

    QWidget *leftWidget = new QWidget();
    QVBoxLayout *leftVLayout = new QVBoxLayout(leftWidget);

    int totalWorkpiece = 120;
    int errorCount = 8;
    double successRate = (totalWorkpiece - errorCount) / (double)totalWorkpiece * 100.0;
    QLabel *statLabel = new QLabel(QString("今日工件总数：%1\n异常数量：%2\n成功率：%3%")
                                       .arg(totalWorkpiece)
                                       .arg(errorCount)
                                       .arg(successRate, 0, 'f', 2));
    leftVLayout->addWidget(statLabel);

    QStringList imgFileList = {"test.jpg", "test1.jpg", "test2.jpg"};
    imgLabel = new QLabel;

    for(int i=0; i<imgFileList.size(); i++)
    {
        QPushButton *btn = new QPushButton(QString("异常%1").arg(i+1));
        leftVLayout->addWidget(btn);

        connect(btn, &QPushButton::clicked, [=](){
            QPixmap pix = ImageDisplayHelper::loadAndScaleImage(imgFileList[i],600,338);
            if(pix.isNull())
            {
                imgLabel->setText(QString("❌ %1 加载失败！").arg(imgFileList[i]));
                imgLabel->setPixmap(QPixmap());
            }
            else
            {
                imgLabel->setPixmap(pix);
            }
        });
    }
    leftVLayout->addStretch();

    QPixmap initPix = ImageDisplayHelper::loadAndScaleImage(imgFileList[0],600,338);
    if(initPix.isNull())
    {
        imgLabel->setText("❌ test.jpg 图片加载失败！检查文件名/权限");
    }
    else
    {
        imgLabel->setPixmap(initPix);
    }

    SqliteDbHelper dbHelper;
    bool dbOk = dbHelper.openDb("test.db");
    QLabel* dbLabel;
    if(dbOk)
    {
        dbLabel = new QLabel("✅ SQLite数据库打开成功");
    }
    else
    {
        dbLabel = new QLabel("❌ SQLite失败："+dbHelper.getErrorMsg());
    }
    leftVLayout->addWidget(dbLabel);

    mainHLayout->addWidget(leftWidget);
    mainHLayout->addWidget(imgLabel);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    DefectMainWindow w;
    w.show();

    return app.exec();
}
