#pragma once

#include <QWidget>
#include <QtCore/QProcess>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

#include "ui_qtmonkey_gui.h"

class QtMonkeyAppCtrl
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
signals:
    void monkeyAppFinishedSignal(QString msg);
    void monkeyAppNewEvent(const QString &scriptLine);

public:
    explicit QtMonkeyAppCtrl(const QString &appPath, const QStringList &appArgs,
                             QObject *parent = nullptr);
private slots:
    void monkeyAppError(QProcess::ProcessError);
    void monkeyAppFinished(int, QProcess::ExitStatus);
    void monkeyAppNewOutput();
    void monkeyAppNewErrOutput();

private:
    QProcess qtmonkeyApp_;
    std::string jsonFromMonkey_;
};

class QtMonkeyWindow
#ifndef Q_MOC_RUN
    final
#endif
    : public QWidget,
      private Ui::QtMonkeyMainWin
{
    Q_OBJECT
public:
    QtMonkeyWindow(QWidget *parent = nullptr);
private slots:
    void on_pbStartRecording__pressed();
    void on_leTestApp__textEdited(const QString &text);
    void on_leTestAppArgs__textEdited(const QString &text);
    void on_pbBrowse__pressed();

    void onMonkeyAppFinishedSignal(QString);
    void savePrefs();
    void onMonkeyAppNewEvent(const QString &scriptLine);

private:
    QtMonkeyAppCtrl *monkeyCtrl_ = nullptr;
    QTimer savePrefsTimer_;

    QtMonkeyAppCtrl *getMonkeyCtrl();
    void showError(const QString &msg);
    void loadPrefs();
    void scheduleSave();
};
