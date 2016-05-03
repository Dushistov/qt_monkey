#pragma once

#include <QPushButton>

class MyCustomButton : public QPushButton {
    Q_OBJECT
public:
    MyCustomButton(QWidget *parent): QPushButton(parent) {}
};
