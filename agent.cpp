#include "agent.hpp"

#include <QtCore/QCoreApplication>

#include "user_events_analyzer.hpp"

using namespace qt_monkey;

Agent::Agent(std::list<CustomEventAnalyzer> customEventAnalyzers)
    : eventAnalyzer_(
          new UserEventsAnalyzer(std::move(customEventAnalyzers), this))
{
    connect(eventAnalyzer_, SIGNAL(userEventInScriptForm(const QString &)),
            this, SLOT(onUserEventInScriptForm(const QString &)));
	QCoreApplication::instance()->installEventFilter(eventAnalyzer_);
}

Agent::~Agent()
{
}

void Agent::onUserEventInScriptForm(const QString &script)
{
    qDebug("%s: script '%s'\n", Q_FUNC_INFO, qPrintable(script));
}
