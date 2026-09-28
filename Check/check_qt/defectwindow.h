#ifndef DEFECTWINDOW_H
#define DEFECTWINDOW_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QPixmap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QString>

//图片辅助类声明
class ImageDisplayHelper
{
public:
    static QPixmap loadAndScaleImage(const QString& filePath, int targetW, int targetH);
};

//数据库工具类声明
class SqliteDbHelper
{
public:
    SqliteDbHelper();
    ~SqliteDbHelper();
    bool openDb(const QString& dbFileName);
    QString getErrorMsg() const;
private:
    QSqlDatabase db;
};

//主窗口类声明
class DefectMainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DefectMainWindow(QWidget *parent = nullptr);

private:
    QLabel* imgLabel = nullptr;
};

#endif // DEFECTWINDOW_H
