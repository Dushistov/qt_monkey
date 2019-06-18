#pragma once

#include <atomic>
#include <cassert>
#include <map>

#include <QKeySequence>
#include <QtCore/QEvent>
#include <QtCore/QObject>

#include "custom_event_analyzer.hpp"
#include "custom_script_extension.hpp"
#include "semaphore.hpp"
#include "shared_resource.hpp"

class QAction;
class QThread;

namespace qt_monkey_agent
{

class UserEventsAnalyzer;

namespace Private
{
class Script;
class ScriptRunner;
class MacMenuActionWatcher;
} // namespace Private
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
     * @param showObjectShortcut shorutcut key to show object info under mouse
     * cursor
     * @param customEventAnalyzers custom event analyzers, it is possible
     * @param populateScriptContext gives you ability to introduce
     * new functions or objects for scripts
     */
    explicit Agent(const QKeySequence &showObjectShortcut
                   = QKeySequence(Qt::Key_F12 | Qt::SHIFT),
                   std::list<CustomEventAnalyzer> customEventAnalyzers
                   = std::list<CustomEventAnalyzer>(),
                   PopulateScriptContext populateScriptContext = {});
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
    //! throw exception inside script
    void throwScriptError(QString msg);
    void setDemonstrationMode(bool val) { demonstrationMode_ = val; }
    bool demonstrationMode() const { return demonstrationMode_; }
    void setTraceEnabled(bool val) { scriptTracingMode_ = val; }
    void saveScreenshots(const QString &path, int nSteps);
    static Agent *instance() { return gAgent_; }
private slots:
    void onUserEventInScriptForm(const QString &);
    void onCommunicationError(const QString &);
    void onRunScriptCommand(const qt_monkey_agent::Private::Script &);
    void onAppAboutToQuit();
    void onScriptLog(const QString &);

private:
    friend class Private::MacMenuActionWatcher;
    friend class ScriptAPI;

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
    qt_monkey_common::Semaphore guiRunSem_{0};
    PopulateScriptContext populateScriptContextCallback_;
    static Agent *gAgent_;
    std::atomic<bool> demonstrationMode_{false};
    std::atomic<bool> scriptTracingMode_{false};
    qt_monkey_common::SharedResource<std::multimap<QString, QAction *>>
        menuItemsOnMac_;
    qt_monkey_common::SharedResource<std::pair<QString, int>> screenshots_;
    QString scriptBaseName_;

    void customEvent(QEvent *event) override;
};
} // namespace qt_monkey_agent
