#pragma once

#include <QtCore/QObject>

class ScriptExt : public QObject
{
    Q_OBJECT
public:
    explicit ScriptExt(QObject *parent = nullptr) : QObject(parent) {}
public slots:
    void pressButton(const QString &caption);
};
