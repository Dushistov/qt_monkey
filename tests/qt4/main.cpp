#include <QtGui/QApplication>

#include "agent.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qt_monkey::Agent agent;
    return app.exec();
}
