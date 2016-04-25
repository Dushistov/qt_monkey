#include "qtmonkey.hpp"

#include <QtCore/QCoreApplication>

#include "common.hpp"

namespace
{
static QString processErrorToString(QProcess::ProcessError err)
{
	switch (err) {
	case QProcess::FailedToStart:
		return T_("Process failed to start");
	case QProcess::Crashed:
		return T_("Process crashed");
	default:
		return T_("Unknown error process error");
	}
}
}

QtMonkey::QtMonkey(QString userAppPath, QStringList userAppArgs)
{
    connect(&userApp_, SIGNAL(error(QProcess::ProcessError)), this,
            SLOT(userAppError(QProcess::ProcessError)));
    connect(&userApp_, SIGNAL(finished(int, QProcess::ExitStatus)), this,
            SLOT(userAppFinished(int, QProcess::ExitStatus)));
    connect(&userApp_, SIGNAL(readyReadStandardOutput()), this,
            SLOT(userAppNewOutput()));
    connect(&userApp_, SIGNAL(readyReadStandardError()), this,
            SLOT(userAppNewErrOutput()));

    connect(&channelWithAgent_, SIGNAL(error(const QString&)), this, SLOT(communicationWithAgentError(const QString&)));
    connect(&channelWithAgent_, SIGNAL(newUserAppEvent(QString)), this, SLOT(onNewUserAppEvent(QString)));
    userApp_.start(userAppPath, userAppArgs);
}

void QtMonkey::communicationWithAgentError(const QString &errStr)
{
    qWarning("%s: errStr %s", Q_FUNC_INFO, qPrintable(errStr));
}

void QtMonkey::onNewUserAppEvent(QString scriptLines)
{
    qDebug("%s: scriptLines %s", Q_FUNC_INFO, qPrintable(scriptLines));
}

void QtMonkey::userAppError(QProcess::ProcessError err)
{
    qDebug("%s: begin err %d", Q_FUNC_INFO, static_cast<int>(err));
    throw std::runtime_error(qPrintable(processErrorToString(err)));
}

void QtMonkey::userAppFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug("%s: begin exitCode %d, exitStatus %d", Q_FUNC_INFO, exitCode, static_cast<int>(exitStatus));
    if (exitCode != EXIT_SUCCESS)
        throw std::runtime_error(T_("user app exit status not %1: %2").arg(EXIT_SUCCESS).arg(exitCode).toUtf8().data());
    QCoreApplication::exit(EXIT_SUCCESS);
}

void QtMonkey::userAppNewOutput()
{
}

void QtMonkey::userAppNewErrOutput()
{
}
