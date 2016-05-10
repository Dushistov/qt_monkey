#pragma once

#include <functional>
#include <string>

#include <QtCore/QString>

namespace json11
{
class string_view;
}

namespace qt_monkey_app
{
std::string createPacketFromUserAppEvent(const QString &scriptLines);
std::string createPacketFromUserAppErrors(const QString &errOut);
std::string createPacketFromScriptEnd();
std::string createPacketFromUserAppScriptLog(const QString &logMsg);
std::string createPacketFromRunScript(const QString &script,
                                      const QString &scriptFileName);

void parseOutputFromGui(
    const json11::string_view &data, size_t &parserStopPos,
    const std::function<void(QString, QString)> &onRunScript,
    const std::function<void(QString)> &onParseError);

void parseOutputFromMonkeyApp(
    const json11::string_view &data, size_t &stopPos,
    const std::function<void(QString)> &onNewUserAppEvent,
    const std::function<void(QString)> &onUserAppError,
    const std::function<void()> &onScriptEnd,
    const std::function<void(QString)> &onScriptLog,
    const std::function<void(QString)> &onParseError);
}
