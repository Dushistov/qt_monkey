#include "mainwin.hpp"

MainWin::MainWin()
{
    setupUi(this);
}

void MainWin::on_actionQuit_triggered()
{
    qApp->quit();
}
