#pragma once

#include <cassert>

#include <QKeySequence>
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QSemaphore>

#include "custom_event_analyzer.hpp"

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
class ScriptRunner;
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
    explicit Agent(const QKeySequence &showObjectShortcut = QKeySequence(Qt::Key_F12 | Qt::SHIFT),
                   std::list<CustomEventAnalyzer> customEventAnalyzers = {});
    ~Agent();
    Agent(const Agent &) = delete;
    Agent &operator=(const Agent &) = delete;
    //! send log message to monkey
    void sendToLog(QString msg);
    //! called from script code for break point purposes
    void scriptCheckPoint();

    //@{
    /**
     * Run function in GUI thread, and wait it completition
     * @param func function to run inside GUI thread
     * @return error message if error appear or empty string if all ok
     */
    QString runCodeInGuiThreadSync(std::function<QString()> func);
    QString runCodeInGuiThreadSyncWithTimeout(std::function<QString()> func,
                                              int timeoutSecs);
    //@}
    void throwScriptError(QString msg);
private slots:
    void onUserEventInScriptForm(const QString &);
    void onCommunicationError(const QString &);
    void onRunScriptCommand(const qt_monkey_agent::Private::Script &);
    void onAppAboutToQuit();
    void onScriptLog(const QString &);
private:
    struct CurrentScriptContext final {
        CurrentScriptContext(Private::ScriptRunner *cur,
                             Private::ScriptRunner *&global)
            : global_(global)
        {
            assert(global_ == nullptr);
            global_ = cur;
        }
        ~CurrentScriptContext() { global_ = nullptr; }
    private:
        Private::ScriptRunner *&global_;
    };

    qt_monkey_agent::UserEventsAnalyzer *eventAnalyzer_ = nullptr;
    QThread *thread_ = nullptr;
    Private::ScriptRunner *curScriptRunner_ = nullptr;
    QEvent::Type eventType_;
    QSemaphore guiRunSem_{0};

    void customEvent(QEvent *event) override;
};
}
