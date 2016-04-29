#pragma once

#include "custom_event_analyzer.hpp"
#include <QtCore/QObject>

class QThread;
namespace qt_monkey_agent
{
class UserEventsAnalyzer;
}

namespace qt_monkey_agent
{

namespace Private
{
class Script;
}
/**
 * This class is used as agent inside user's program
 * to catch/apply Qt events
 */
class Agent
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
public:
    /**
     * using QApplication::installEventFilter, so it should be after all
     * other calls to QApplication::installEventFilter in user app
     * @param customEventAnalyzers custom event analyzers, it is possible
     * to use them as event analyzer extension point
     */
    explicit Agent(std::list<CustomEventAnalyzer> customEventAnalyzers = {});
    ~Agent();
    Agent(const Agent &) = delete;
    Agent &operator=(const Agent &) = delete;
    void sendToLog(QString msg);
private slots:
    void onUserEventInScriptForm(const QString &);
    void onCommunicationError(const QString &);
    void onRunScriptCommand(const qt_monkey_agent::Private::Script &);

private:
    qt_monkey_agent::UserEventsAnalyzer *eventAnalyzer_;
    QThread *thread_;
};
}
