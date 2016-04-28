#pragma once

#include <queue>
#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QProcess>

#include "agent_qtmonkey_communication.hpp"
#include "script.hpp"

namespace qt_monkey_app
{
//! main class to control agent
class QtMonkey
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
public:
    QtMonkey();
    ~QtMonkey();
    void runApp(QString userAppPath, QStringList userAppArgs) { userApp_.start(userAppPath, userAppArgs); }
    bool runScriptFromFile(QStringList scriptPathList, const char *encoding = "UTF-8");
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
private:
    qt_monkey_agent::Private::CommunicationMonkeyPart channelWithAgent_;
    QProcess userApp_;
    QTextStream cout_;
    QTextStream cerr_;
    std::string stdinBuf_;
    std::queue<qt_monkey_agent::Private::Script> toRunList_;
};
}
