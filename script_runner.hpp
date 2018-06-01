#pragma once

#include <QtScript/QScriptEngine>

#include "custom_script_extension.hpp"

namespace qt_monkey_agent
{
class ScriptAPI;

namespace Private
{
class Script;

class ScriptRunner final
{
public:
    explicit ScriptRunner(ScriptAPI &api,
                          const PopulateScriptContext &onInitCb);
    void runScript(const Script &, QString &errMsg);
    int currentLineNum() const;
    void throwError(QString errMsg);

private:
    QScriptEngine scriptEngine_;
};
} // namespace Private
} // namespace qt_monkey_agent
