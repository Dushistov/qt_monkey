#pragma once

#include <QtCore/QString>
#include <functional>

namespace qt_monkey_app
{
QByteArray createPacketFromUserAppEvent(const QString &scriptLines);
QByteArray createPacketFromUserAppErrors(const QString &errOut);
QByteArray createPacketFromScriptEnd();
QByteArray createPacketFromUserAppScriptLog(const QString &logMsg);
QByteArray createPacketFromRunScript(const QString &script,
                                     const QString &scriptFileName);

void parseOutputFromGui(
    const QByteArray &data, size_t &parserStopPos,
    const std::function<void(QString, QString)> &onRunScript,
    const std::function<void(QString)> &onParseError);

void parseOutputFromMonkeyApp(
    const QByteArray &data, size_t &stopPos,
    const std::function<void(QString)> &onNewUserAppEvent,
    const std::function<void(QString)> &onUserAppError,
    const std::function<void()> &onScriptEnd,
    const std::function<void(QString)> &onScriptLog,
    const std::function<void(QString)> &onParseError);
}
