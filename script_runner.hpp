#pragma once

#include <QtScript/QScriptEngine>

namespace qt_monkey_agent
{
class ScriptAPI;

namespace Private
{
class Script;

class ScriptRunner final
{
public:
    explicit ScriptRunner(ScriptAPI &api, QObject *apiExtension = nullptr);
    void runScript(const Script &, QString &errMsg);
    int currentLineNum() const;
private:
    QScriptEngine scriptEngine_;
};
}
}
