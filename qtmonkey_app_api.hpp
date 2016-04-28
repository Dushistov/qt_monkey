#pragma once

#include <QtCore/QString>
#include <functional>

namespace qt_monkey_app
{
extern QByteArray userAppEventToFromMonkeyAppPacket(const QString &scriptLines);
extern void parseOutputFromMonkeyApp(
    const QByteArray &data, size_t &stopPos,
    const std::function<void(const QString &)> &onNewUserAppEvent,
    const std::function<void(const QString &)> &onParseError);
}
