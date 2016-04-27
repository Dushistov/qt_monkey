#pragma once

#include <QMainWindow>

#include "ui_mainwin.h"

class MainWin : public QMainWindow, private Ui::MainWindow {
    Q_OBJECT
public:
    MainWin();
};
