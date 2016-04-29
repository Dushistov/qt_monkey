#include "script_runner.hpp"

#include <cassert>

#include "common.hpp"
#include "script.hpp"
#include "script_api.hpp"

using qt_monkey_agent::Private::ScriptRunner;
using qt_monkey_agent::Private::Script;

ScriptRunner::ScriptRunner(ScriptAPI &api, QObject *extension)
{
	QScriptValue testCtrl = scriptEngine_.newQObject(&api);
	QScriptValue global = scriptEngine_.globalObject();

	global.setProperty(QLatin1String("Test"), testCtrl);
}

int qt_monkey_agent::extractLineNumFromBacktraceLine(const QString& line)
{
    const int ln = line.indexOf(':');
    assert(ln != -1);

    return line.right(line.size() - ln - 1).toUInt();	
}

void ScriptRunner::runScript(const Script &script, QString &errMsg)
{
    scriptEngine_.evaluate(script.code(), "script", 1);

	if (scriptEngine_.hasUncaughtException()) {
		QString expd;

		expd += QStringLiteral("Backtrace:\n");
		const QStringList backtrace = scriptEngine_.uncaughtExceptionBacktrace();
		expd += backtrace.join("\n") + '\n';

		int elino = 0;
		if (!backtrace.empty()) {
			elino = extractLineNumFromBacktraceLine(backtrace.back());
		} else {
			elino = scriptEngine_.uncaughtExceptionLineNumber();
		}

		const QStringList slines = script.code().split('\n');

		if (elino <= slines.size())
			expd += QString("Line which throw exception: %1\n").arg(slines[elino - 1]);

		expd += QString("Exception: %1")
			.arg(scriptEngine_.uncaughtException().toString());

        errMsg = expd;
	}
}
