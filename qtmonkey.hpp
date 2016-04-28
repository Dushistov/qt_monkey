#pragma once

#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QProcess>

#include "agent_qtmonkey_communication.hpp"

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
    QtMonkey(QString userAppPath, QStringList userAppArgs);
private slots:
    void userAppError(QProcess::ProcessError);
    void userAppFinished(int, QProcess::ExitStatus);
    void userAppNewOutput();
    void userAppNewErrOutput();
    void communicationWithAgentError(const QString &errStr);
    void onNewUserAppEvent(QString scriptLines);
    void stdinDataReady();

private:
    qt_monkey_agent::Private::CommunicationMonkeyPart channelWithAgent_;
    QProcess userApp_;
    QTextStream cout_;
    QTextStream cerr_;
    std::string stdinBuf_;
};
}
