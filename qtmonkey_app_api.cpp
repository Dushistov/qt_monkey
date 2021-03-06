#include "qtmonkey_app_api.hpp"

#include <cassert>

#include "common.hpp"
#include "json11.hpp"

namespace qt_monkey_app
{

using json11::Json;

namespace
{
struct QStringJsonTrait final {
    explicit QStringJsonTrait(const QString &s) : str_(s) {}
    std::string to_json() const { return str_.toUtf8().data(); }

private:
    const QString &str_;
};
} // namespace

std::string createPacketFromUserAppEvent(const QString &scriptLines)
{
    auto json = Json::object{
        {"event", Json::object{{"script", QStringJsonTrait{scriptLines}}}}};

    return Json{json}.dump();
}

std::string createPacketFromUserAppOutput(const QString &stdOutLines)
{
    auto json = Json::object{{"app output", QStringJsonTrait{stdOutLines}}};
    return Json{json}.dump();
}

std::string createPacketFromUserAppErrors(const QString &errMsg)
{
    auto json = Json::object{{"app errors", QStringJsonTrait{errMsg}}};
    return Json{json}.dump();
}

std::string createPacketFromScriptEnd()
{
    auto json = Json{"script end"};
    return Json{json}.dump();
}

std::string createPacketFromUserAppScriptLog(const QString &logMsg)
{
    auto json = Json::object{{"script logs", QStringJsonTrait{logMsg}}};
    return Json{json}.dump();
}

std::string createPacketFromRunScript(const QString &script,
                                      const QString &scriptFileName)
{
    auto json = Json::object{
        {"run script",
         Json::object{{"script", QStringJsonTrait{script}},
                      {"file", QStringJsonTrait{scriptFileName}}}}};
    return Json{json}.dump();
}

void parseOutputFromMonkeyApp(
    const json11::string_view &data, size_t &stopPos,
    const std::function<void(QString)> &onNewUserAppEvent,
    const std::function<void(QString)> &onUserAppError,
    const std::function<void()> &onScriptEnd,
    const std::function<void(QString)> &onScriptLog,
    const std::function<void(QString)> &onParseError)
{
    assert(data.size() > 0);
    stopPos = 0;
    std::string::size_type parserStopPos;
    std::string err;
    auto jsonArr = Json::parse_multi(data, parserStopPos, err);
    stopPos = parserStopPos;
    for (const Json &elm : jsonArr) {
        if (elm.is_null())
            continue;
        if (elm.is_object() && elm.object_items().size() == 1u
            && elm.object_items().begin()->first == "event") {
            const Json &eventJson = elm.object_items().begin()->second;
            if (!eventJson.is_object() || eventJson.object_items().size() != 1u
                || eventJson.object_items().begin()->first != "script"
                || !eventJson.object_items().begin()->second.is_string()) {
                onParseError(QStringLiteral("event"));
                return;
            }
            onNewUserAppEvent(QString::fromUtf8(eventJson.object_items()
                                                    .begin()
                                                    ->second.string_value()
                                                    .c_str()));
        } else if (elm.is_object() && elm.object_items().size() == 1u
                   && elm.object_items().begin()->first == "app errors") {
            auto it = elm.object_items().begin();
            if (!it->second.is_string()) {
                onParseError(QStringLiteral("app errors"));
                return;
            }
            onUserAppError(
                QString::fromUtf8(it->second.string_value().c_str()));
        } else if (elm.is_object() && elm.object_items().size() == 1u
                   && elm.object_items().begin()->first == "script logs") {
            auto it = elm.object_items().begin();
            if (!it->second.is_string()) {
                onParseError(QStringLiteral("script logs"));
                return;
            }
            onScriptLog(QString::fromUtf8(it->second.string_value().c_str()));
        } else if (elm.is_string() && elm.string_value() == "script end") {
            onScriptEnd();
        }
    }
}

void parseOutputFromGui(
    const json11::string_view &data, size_t &parserStopPos,
    const std::function<void(QString, QString)> &onRunScript,
    const std::function<void(QString)> &onParseError)
{
    std::string err;
    auto jsonArr = Json::parse_multi(data, parserStopPos, err);
    for (const Json &elm : jsonArr) {
        if (elm.is_null())
            continue;
        if (elm.is_object() && elm.object_items().size() == 1u
            && elm.object_items().begin()->first == "run script") {
            const Json &scriptJson = elm.object_items().begin()->second;

            Json::object::const_iterator scriptIt, scriptFNameIt;
            if (!scriptJson.is_object()
                || scriptJson.object_items().size() != 2u
                || (scriptIt = scriptJson.object_items().find("script"))
                       == scriptJson.object_items().end()
                || !scriptIt->second.is_string()
                || (scriptFNameIt = scriptJson.object_items().find("file"))
                       == scriptJson.object_items().end()
                || !scriptFNameIt->second.is_string()) {
                onParseError(QStringLiteral("run script"));
                return;
            }

            onRunScript(
                QString::fromUtf8(scriptIt->second.string_value().c_str()),
                QString::fromUtf8(
                    scriptFNameIt->second.string_value().c_str()));
        }
    }
}
} // namespace qt_monkey_app
