//#define DEBUG_AGENT
#include "agent.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>

#include <QApplication>
#include <QtCore/QAbstractEventDispatcher>
#include <QtCore/QThread>
#include <QWidget>

#include "agent_qtmonkey_communication.hpp"
#include "common.hpp"
#include "script.hpp"
#include "script_api.hpp"
#include "script_runner.hpp"
#include "user_events_analyzer.hpp"

using qt_monkey_agent::Agent;
using qt_monkey_agent::Private::PacketTypeForMonkey;
using qt_monkey_agent::Private::CommunicationAgentPart;
using qt_monkey_agent::Private::ScriptRunner;

Agent *Agent::gAgent_ = nullptr;

#define GET_THREAD(__name__)                                                   \
    auto __name__ = static_cast<AgentThread *>(thread_);                       \
    if (__name__->isFinished()) {                                              \
        return;                                                                \
    }

#ifdef DEBUG_AGENT
#define DBGPRINT(fmt, ...) qDebug(fmt, __VA_ARGS__)
#else
#define DBGPRINT(fmt, ...)                                                     \
    do {                                                                       \
    } while (false)
#endif

namespace
{
class FuncEvent final : public QEvent
{
public:
    FuncEvent(QEvent::Type type, std::function<void()> func)
        : QEvent(type), func_(std::move(func))
    {
    }
    void exec() { func_(); }
private:
    std::function<void()> func_;
};

class EventsReciever final : public QObject
{
public:
    EventsReciever()
    {
        eventType_ = static_cast<QEvent::Type>(QEvent::registerEventType());
    }
    void customEvent(QEvent *event) override
    {
        if (event->type() != eventType_)
            return;

        static_cast<FuncEvent *>(event)->exec();
    }
    QEvent::Type eventType() const { return eventType_; }
private:
    std::atomic<QEvent::Type> eventType_;
};

class AgentThread final : public QThread
{
public:
    AgentThread(QObject *parent) : QThread(parent) {}
    bool isNotReady() { return !ready_.exchange(false); }
    void run() override
    {
        CommunicationAgentPart client;
        if (!client.connectToMonkey()) {
            qWarning(
                "%s",
                qPrintable(
                    T_("%1: can not connect to qt monkey").arg(Q_FUNC_INFO)));
            return;
        }
        connect(&client, SIGNAL(error(const QString &)), parent(),
                SLOT(onCommunicationError(const QString &)),
                Qt::DirectConnection);
        connect(&client,
                SIGNAL(runScript(const qt_monkey_agent::Private::Script &)),
                parent(), SLOT(onRunScriptCommand(
                              const qt_monkey_agent::Private::Script &)),
                Qt::DirectConnection);
        EventsReciever eventReciever;
        objInThread_ = &eventReciever;
        channelWithMonkey_ = &client;
        ready_ = true;
        exec();
    }

    CommunicationAgentPart *channelWithMonkey() { return channelWithMonkey_; }

    void runInThread(std::function<void()> func)
    {
        assert(objInThread_ != nullptr);
        QCoreApplication::postEvent(
            objInThread_,
            new FuncEvent(objInThread_->eventType(), std::move(func)));
    }

private:
    std::atomic<bool> ready_{false};
    EventsReciever *objInThread_{nullptr};
    CommunicationAgentPart *channelWithMonkey_{nullptr};
};
}

Agent::Agent(const QKeySequence &showObjectShortcut,
             std::list<CustomEventAnalyzer> customEventAnalyzers,
             PopulateScriptContext psc)
    : eventAnalyzer_(new UserEventsAnalyzer(
          *this, showObjectShortcut, std::move(customEventAnalyzers), this)),
      populateScriptContextCallback_(std::move(psc))
{
    // make sure that type is referenced, fix bug with qt4 and static lib
    qMetaTypeId<qt_monkey_agent::Private::Script>();
    eventType_ = static_cast<QEvent::Type>(QEvent::registerEventType());
    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(onAppAboutToQuit()));
    connect(eventAnalyzer_, SIGNAL(userEventInScriptForm(const QString &)),
            this, SLOT(onUserEventInScriptForm(const QString &)));
    connect(eventAnalyzer_, SIGNAL(scriptLog(const QString &)), this,
            SLOT(onScriptLog(const QString &)));
    QCoreApplication::instance()->installEventFilter(eventAnalyzer_);
    thread_ = new AgentThread(this);
    thread_->start();
    while (!thread_->isFinished()
           && static_cast<AgentThread *>(thread_)->isNotReady())
        ;
    assert(gAgent_ == nullptr);
    gAgent_ = this;
}

void Agent::onCommunicationError(const QString &err)
{
    qFatal("%s: communication error %s", Q_FUNC_INFO, qPrintable(err));
    std::abort();
}

Agent::~Agent()
{
    GET_THREAD(thread)

    thread->runInThread(
        [this, thread] { thread->channelWithMonkey()->flushSendData(); });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1000 /*ms*/);
    thread->quit();
    thread->wait();
}

void Agent::onUserEventInScriptForm(const QString &script)
{
    if (script.isEmpty())
        return;
    GET_THREAD(thread)
    thread->channelWithMonkey()->sendCommand(
        PacketTypeForMonkey::NewUserAppEvent, script);
}

void Agent::onRunScriptCommand(const Private::Script &script)
{
    GET_THREAD(thread)
    assert(QThread::currentThread() == thread_);
    DBGPRINT("%s: run script", Q_FUNC_INFO);
    ScriptAPI api{*this};
    ScriptRunner sr{api, populateScriptContextCallback_};
    QString errMsg;
    {
        CurrentScriptContext context(&sr, curScriptRunner_);
        sr.runScript(script, errMsg);
    }
    if (!errMsg.isEmpty()) {
        qWarning("AGENT: %s: script return error", Q_FUNC_INFO);
        thread->channelWithMonkey()->sendCommand(
            PacketTypeForMonkey::ScriptError, errMsg);
    } else {
        DBGPRINT("%s: sync with gui", Q_FUNC_INFO);
        // if all ok, sync with gui, so user recieve all events
        // before script exit
        runCodeInGuiThreadSync([] {
            qt_monkey_common::processEventsFor(300 /*ms*/);
            DBGPRINT("%s: wait done", Q_FUNC_INFO);
            return QString();
        });
    }
    DBGPRINT("%s: report about script end", Q_FUNC_INFO);
    thread->channelWithMonkey()->sendCommand(PacketTypeForMonkey::ScriptEnd,
                                             QString());
}

void Agent::sendToLog(QString msg)
{
    DBGPRINT("%s: msg %s", Q_FUNC_INFO, qPrintable(msg));
    GET_THREAD(thread)
    thread->channelWithMonkey()->sendCommand(PacketTypeForMonkey::ScriptLog,
                                             std::move(msg));
}

void Agent::scriptCheckPoint()
{
    assert(QThread::currentThread() == thread_);
    assert(curScriptRunner_ != nullptr);
    const int lineno = curScriptRunner_->currentLineNum();
    DBGPRINT("%s: lineno %d", Q_FUNC_INFO, lineno);
    if (scriptTracingMode_)
        sendToLog(QStringLiteral("reached %1 line").arg(lineno));
}

QString Agent::runCodeInGuiThreadSync(std::function<QString()> func)
{
    assert(QThread::currentThread() == thread_);
    QString res;
    QCoreApplication::postEvent(this,
                                new FuncEvent(eventType_, [func, this, &res] {
                                    res = func();
                                    guiRunSem_.release();
                                }));
    guiRunSem_.acquire();
    return res;
}

void Agent::customEvent(QEvent *event)
{
    assert(QThread::currentThread() != thread_);
    if (event->type() != eventType_)
        return;
    static_cast<FuncEvent *>(event)->exec();
}

void Agent::throwScriptError(QString msg)
{
    assert(QThread::currentThread() == thread_);
    assert(curScriptRunner_ != nullptr);
    curScriptRunner_->throwError(std::move(msg));
}

QString Agent::runCodeInGuiThreadSyncWithTimeout(std::function<QString()> func,
                                                 int timeoutSecs)
{
    assert(QThread::currentThread() == thread_);
    QWidget *wasDialog = nullptr;
    runCodeInGuiThreadSync([&wasDialog] {
        wasDialog = qApp->activeModalWidget();
        return QString();
    });

    if (scriptTracingMode_)
        sendToLog(QStringLiteral("current model widget %1")
                      .arg(wasDialog ? wasDialog->objectName()
                                     : QStringLiteral("null")));

    std::shared_ptr<QSemaphore> waitSem{new QSemaphore};
    std::shared_ptr<QString> res{new QString};
    QCoreApplication::postEvent(this,
                                new FuncEvent(eventType_, [func, waitSem, res] {
                                    *res = func();
                                    waitSem->release();
                                }));

    QWidget *nowDialog = nullptr;
    const QString errMsg = runCodeInGuiThreadSync(
        [&nowDialog] { /*this code send event internally, so
                         if new QEventLoop was created, it will
                         be executed after handling of event posted above
                        */
                       auto dispatcher = QAbstractEventDispatcher::instance(
                           QThread::currentThread());
                       if (dispatcher == nullptr)
                           return QStringLiteral("no event dispatcher");
                       // if @func cause dialog close, then process all events,
                       // to prevent post of next event to QDialog's QEventLoop
                       dispatcher->processEvents(
                           QEventLoop::ExcludeUserInputEvents);
                       nowDialog = qApp->activeModalWidget();
                       return QString();
        });
    if (!errMsg.isEmpty()) {
        qWarning("%s: get errMsg %s", Q_FUNC_INFO, qPrintable(errMsg));
        return errMsg;
    }
    if (scriptTracingMode_)
        sendToLog(QStringLiteral("current model widget %1, wait %2")
                      .arg(nowDialog ? nowDialog->objectName()
                                     : QStringLiteral("null"))
                  .arg(nowDialog != wasDialog ? "with timeout" : "endless"));

    if (nowDialog != wasDialog) {
        DBGPRINT("%s: dialog has changed\n", Q_FUNC_INFO);
        // it may not return, if @func cause new QEventLoop creation, so
        const int timeoutMsec = timeoutSecs * 1000;
        const int waitIntervalMsec = 100;
        const int N = timeoutMsec / waitIntervalMsec + 1;
        for (int attempt = 0; attempt < N; ++attempt) {
            if (waitSem->tryAcquire(1, waitIntervalMsec))
                return *res;
        }
        DBGPRINT("%s: timeout occuire", Q_FUNC_INFO);
    } else {
        DBGPRINT("%s: wait of finished event handling", Q_FUNC_INFO);
        waitSem->acquire();
        DBGPRINT("%s: wait of finished event handling DONE", Q_FUNC_INFO);
    }
    if (scriptTracingMode_)
        sendToLog(QStringLiteral("run in gui thread done"));

    return QString();
}

void Agent::onAppAboutToQuit()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    qt_monkey_common::processEventsFor(300 /*ms*/);
}

void Agent::onScriptLog(const QString &msg)
{
    assert(QThread::currentThread() != thread_);
    GET_THREAD(thread)
    thread->channelWithMonkey()->sendCommand(PacketTypeForMonkey::ScriptLog,
                                             msg);
}
