#include <QApplication>
#include <QDesktopWidget>

#include "agent.hpp"
#include "mainwin.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qt_monkey::Agent agent;
    MainWin mainwin;
    const QDesktopWidget *desc = QApplication::desktop();
    mainwin.resize(desc->width() / 4, desc->height() / 4);
    mainwin.show();
    return app.exec();
}
