#pragma once

#include <QtScript/QScriptEngine>

namespace qt_monkey_agent
{
namespace Private
{

class Script;

class ScriptRunner final
{
public:
    explicit ScriptRunner(QObject *extension = nullptr);
    void runScript(const Script &, QString &errMsg);
private:
    QScriptEngine scriptEngine_;
};
}
}
