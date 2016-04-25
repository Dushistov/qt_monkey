#pragma once

#include <QtCore/QObject>
#include "custom_event_analyzer.hpp"

class QThread;
namespace qt_monkey
{
class UserEventsAnalyzer;
}

namespace qt_monkey
{
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
private slots:
    void onUserEventInScriptForm(const QString &);
    void onCommunicationError(const QString &);
private:
    qt_monkey::UserEventsAnalyzer *eventAnalyzer_;
    QThread *thread_;
};
}
