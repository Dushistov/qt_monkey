#pragma once

#include <QMainWindow>

#include "ui_mainwin.h"

class MainWin : public QMainWindow, private Ui::MainWindow {
    Q_OBJECT
public:
    MainWin();
private slots:
    void on_actionQuit_triggered();
    void on_pushButton_ModalDialog_pressed();
};
