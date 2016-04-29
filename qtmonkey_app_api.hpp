#pragma once

#include <QtCore/QString>
#include <functional>

namespace qt_monkey_app
{
extern QByteArray createPacketFromUserAppEvent(const QString &scriptLines);
extern QByteArray createPacketFromUserAppErrors(const QString &errOut);
extern QByteArray createPacketFromScriptEnd();
extern QByteArray createPacketFromUserAppScriptLog(const QString &logMsg);

extern void parseOutputFromMonkeyApp(
    const QByteArray &data, size_t &stopPos,
    const std::function<void(QString)> &onNewUserAppEvent,
    const std::function<void(QString)> &onUserAppError,
    const std::function<void()> &onScriptEnd,
    const std::function<void(QString)> &onScriptLog,
    const std::function<void(QString)> &onParseError);
}
