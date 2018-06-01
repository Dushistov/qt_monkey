#pragma once

#include <queue>

#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QProcess>

#include "agent_qtmonkey_communication.hpp"
#include "script.hpp"
#include "shared_resource.hpp"

namespace qt_monkey_app
{

namespace Private
{
class StdinReader
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
signals:
    void error(const QString &msg);
    void dataReady();

public:
    qt_monkey_common::SharedResource<QByteArray> data;

    void emitError(const QString &msg) { emit error(msg); }
    void emitDataReady() { emit dataReady(); }
};
} // namespace Private
//! main class to control agent
class QtMonkey
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
public:
    explicit QtMonkey(bool exitOnScriptError);
    ~QtMonkey();
    void runApp(QString userAppPath, QStringList userAppArgs)
    {
        userAppPath_ = std::move(userAppPath);
        userAppArgs_ = std::move(userAppArgs);
        userApp_.start(userAppPath_, userAppArgs_);
    }
    bool runScriptFromFile(QString codeToRunBeforeAll,
                           QStringList scriptPathList,
                           const char *encoding = "UTF-8");
private slots:
    void userAppError(QProcess::ProcessError);
    void userAppFinished(int, QProcess::ExitStatus);
    void userAppNewOutput();
    void userAppNewErrOutput();
    void communicationWithAgentError(const QString &errStr);
    void onNewUserAppEvent(QString scriptLines);
    void stdinDataReady();
    void onScriptError(QString errMsg);
    void onAgentReadyToRunScript();
    void onScriptEnd();
    void onScriptLog(QString msg);

private:
    bool scriptRunning_ = false;

    qt_monkey_agent::Private::CommunicationMonkeyPart channelWithAgent_;
    QProcess userApp_;
    std::queue<qt_monkey_agent::Private::Script> toRunList_;
    bool exitOnScriptError_ = false;
    Private::StdinReader stdinReader_;
    QThread *readStdinThread_ = nullptr;
    QString userAppPath_;
    QStringList userAppArgs_;
    bool restartDone_ = false;

    void setScriptRunningState(bool val);
};
} // namespace qt_monkey_app
