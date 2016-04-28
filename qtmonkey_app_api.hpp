#pragma once

#include <QtCore/QString>
#include <functional>

namespace qt_monkey_app
{
extern QByteArray userAppEventToFromMonkeyAppPacket(const QString &scriptLines);
extern QByteArray userAppErrorsToFromMonkeyAppPacket(const QString &errOut);
extern void parseOutputFromMonkeyApp(
    const QByteArray &data, size_t &stopPos,
    const std::function<void(QString)> &onNewUserAppEvent,
    const std::function<void(QString)> &onUserAppError,
    const std::function<void(QString)> &onParseError);
}
