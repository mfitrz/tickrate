#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{

    qDebug() << "SSL supported:" << QSslSocket::supportsSsl();
    qDebug() << "SSL version:" << QSslSocket::sslLibraryVersionString();

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
