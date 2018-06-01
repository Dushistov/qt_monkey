#include "script_runner.hpp"

#include <cassert>

#include "common.hpp"
#include "script.hpp"
#include "script_api.hpp"

using qt_monkey_agent::PopulateScriptContext;
using qt_monkey_agent::ScriptAPI;
using qt_monkey_agent::Private::Script;
using qt_monkey_agent::Private::ScriptRunner;

static int extractLineNumFromBacktraceLine(const QString &line)
{
    const int ln = line.indexOf(':');
    assert(ln != -1);
    return line.right(line.size() - ln - 1).toUInt();
}

ScriptRunner::ScriptRunner(ScriptAPI &api,
                           const PopulateScriptContext &onInitCb)
{
    QScriptValue testCtrl = scriptEngine_.newQObject(&api);
    QScriptValue global = scriptEngine_.globalObject();

    global.setProperty(QLatin1String("Test"), testCtrl);

    if (onInitCb != nullptr)
        onInitCb(scriptEngine_);
}

void ScriptRunner::runScript(const Script &script, QString &errMsg)
{
    scriptEngine_.evaluate(script.code(), "script", 1);

    if (scriptEngine_.hasUncaughtException()) {
        QString expd;

        expd += QStringLiteral("Backtrace:\n");
        const QStringList backtrace
            = scriptEngine_.uncaughtExceptionBacktrace();
        expd += backtrace.join("\n") + '\n';

        int elino = 0;
        if (!backtrace.empty()) {
            elino = extractLineNumFromBacktraceLine(backtrace.back());
        } else {
            elino = scriptEngine_.uncaughtExceptionLineNumber();
        }

        const QStringList slines = script.code().split('\n');

        if (elino <= slines.size())
            expd += QString("Line which throw exception: %1\n")
                        .arg(slines[elino - 1]);

        expd += QString("Exception: %1")
                    .arg(scriptEngine_.uncaughtException().toString());

        errMsg = expd;
    }
}

int ScriptRunner::currentLineNum() const
{
    auto ctx = scriptEngine_.currentContext();
    assert(ctx != nullptr);
    assert(!ctx->backtrace().isEmpty());
    const QStringList backtrace = ctx->backtrace();
    return extractLineNumFromBacktraceLine(backtrace.back());
}

void ScriptRunner::throwError(QString errMsg)
{
    auto ctx = scriptEngine_.currentContext();
    assert(ctx != nullptr);
    ctx->throwError(errMsg);
}
