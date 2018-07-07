//#define DEBUG_AGENT
#include "agent.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>

#include <QApplication>
#include <QWidget>
#include <QtCore/QAbstractEventDispatcher>
#include <QtCore/QDir>
#include <QtCore/QThread>

#include "agent_qtmonkey_communication.hpp"
#include "common.hpp"
#include "script.hpp"
#include "script_api.hpp"
#include "script_runner.hpp"
#include "user_events_analyzer.hpp"

using qt_monkey_agent::Agent;
using qt_monkey_agent::CustomEventAnalyzer;
using qt_monkey_agent::PopulateScriptContext;
using qt_monkey_agent::UserEventsAnalyzer;
using qt_monkey_agent::Private::CommunicationAgentPart;
using qt_monkey_agent::Private::PacketTypeForMonkey;
using qt_monkey_agent::Private::Script;
using qt_monkey_agent::Private::ScriptRunner;
using qt_monkey_common::Semaphore;

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
        connect(
            &client,
            SIGNAL(runScript(const qt_monkey_agent::Private::Script &)),
            parent(),
            SLOT(onRunScriptCommand(const qt_monkey_agent::Private::Script &)),
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

void saveScreenShot(const QString &path)
{
    QWidget *w = QApplication::activeWindow();
    if (w) {
        QPixmap p = QPixmap::grabWidget(w);
        p.save(path);
    }
}

void removeObsolete(const QString &path, unsigned nSteps)
{
    QDir dir(path);
    unsigned i = 0;
    const QStringList dirContent = dir.entryList(QDir::NoFilter, QDir::Time);
    for (const QString &entry : dirContent) {
        const QString entryPath
            = QString("%1%2%3").arg(path).arg(QDir::separator()).arg(entry);
        const QFileInfo fi(entryPath);
        if (!fi.exists() || !fi.isFile()) {
            continue;
        }
        if (i > nSteps) {
            if (!QFile::remove(entryPath)) {
                qWarning("%s: can not remove '%s'\n", Q_FUNC_INFO,
                         qPrintable(entryPath));
            }
        } else {
            ++i;
        }
    }
}
} // namespace

Agent::Agent(const QKeySequence &showObjectShortcut,
             std::list<CustomEventAnalyzer> customEventAnalyzers,
             PopulateScriptContext psc)
    : eventAnalyzer_(new UserEventsAnalyzer(
          *this, showObjectShortcut, std::move(customEventAnalyzers), this)),
      populateScriptContextCallback_(std::move(psc)),
      screenshots_(std::make_pair(QString(), -1))
{
    assert(gAgent_ == nullptr);
    gAgent_ = this;
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
        [thread] { thread->channelWithMonkey()->flushSendData(); });
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

void Agent::onRunScriptCommand(const Script &script)
{
    GET_THREAD(thread)
    assert(QThread::currentThread() == thread_);
    DBGPRINT("%s: run script", Q_FUNC_INFO);
    ScriptAPI api{*this};
    ScriptRunner sr{api, populateScriptContextCallback_};
    QString errMsg;
    {
        CurrentScriptContext context(&sr, curScriptRunner_);
        DBGPRINT("%s: scrit file name %s", Q_FUNC_INFO,
                 qPrintable(script.fileName()));
        QFileInfo fi(script.fileName());
        scriptBaseName_ = fi.baseName();
        sr.runScript(script, errMsg);
    }
    if (!errMsg.isEmpty()) {
        qWarning("AGENT: %s: script return error", Q_FUNC_INFO);
        thread->channelWithMonkey()->sendCommand(
            PacketTypeForMonkey::ScriptError, errMsg);
    } else {
        DBGPRINT("%s: sync with gui", Q_FUNC_INFO);
        // if all ok, sync with gui, so user recieve all events
        // before script exit, add timeout for special case:
        // if it is short configure script before main, and program
        // starts with modal dialog
        runCodeInGuiThreadSyncWithTimeout(
            [] {
                qt_monkey_common::processEventsFor(300 /*ms*/);
                DBGPRINT("%s: wait done", Q_FUNC_INFO);
                return QString();
            },
            10 * 1000);
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

    int nSteps;
    QString savePath;
    {
        auto lock = screenshots_.get();
        nSteps = lock->second;
        savePath = lock->first;
    }

    if (nSteps > 0) {
        removeObsolete(savePath, static_cast<unsigned>(nSteps));
        savePath = QString("%1%2screenshot_%4_%3.png")
                       .arg(savePath)
                       .arg(QDir::separator())
                       .arg(lineno)
                       .arg(scriptBaseName_);
        runCodeInGuiThreadSync([savePath]() -> QString {
            saveScreenShot(savePath);
            return QString();
        });
    }
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

    std::shared_ptr<Semaphore> waitSem{new Semaphore{0}};
    std::shared_ptr<QString> res{new QString};
    qApp->postEvent(this, new FuncEvent(eventType_, [func, waitSem, res] {
                        *res = func();
                        waitSem->release();
                    }));

    // make sure that prev event was handled
    std::shared_ptr<Semaphore> syncSem{new Semaphore{0}};
    qApp->postEvent(this, new FuncEvent(eventType_, [this, syncSem] {
                        syncSem->release();
                        qApp->sendPostedEvents(this, eventType_);
                        syncSem->release();
                    }));
    syncSem->acquire();
    const int timeoutMsec = timeoutSecs * 1000;
    const int waitIntervalMsec = 100;
    const int N = timeoutMsec / waitIntervalMsec + 1;
    int attempt;
    for (attempt = 0; attempt < N / 2; ++attempt) {
        if (syncSem->tryAcquire(1, std::chrono::milliseconds(waitIntervalMsec)))
            break;
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    QWidget *nowDialog = nullptr;
    runCodeInGuiThreadSync([&nowDialog] {
        nowDialog = qApp->activeModalWidget();
        return QString();
    });

    for (unsigned int iter = 0; nowDialog == wasDialog; ++iter) {
        if (waitSem->tryAcquire(1,
                                std::chrono::milliseconds(waitIntervalMsec))) {
            return *res;
        }
        if ((iter % 10) == 0) {
            nowDialog = nullptr;
            runCodeInGuiThreadSync([&nowDialog] {
                nowDialog = qApp->activeModalWidget();
                return QString();
            });
            DBGPRINT("%s: wasDialog %p, nowDialog %p, attempt %d", Q_FUNC_INFO,
                     wasDialog, nowDialog, attempt);
        }
    }

    for (; attempt < N; ++attempt) {
        if (waitSem->tryAcquire(1, std::chrono::milliseconds(waitIntervalMsec)))
            return *res;
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    DBGPRINT("%s: timeout occuire", Q_FUNC_INFO);
    return QString();
}

void Agent::onAppAboutToQuit()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    assert(QThread::currentThread() != thread_);
    GET_THREAD(thread)
    thread->channelWithMonkey()->sendCommand(PacketTypeForMonkey::Close,
                                             QString());
    while (!thread->channelWithMonkey()->hasCloseAck()) {
        qt_monkey_common::processEventsFor(300 /*ms*/);
    }
}

void Agent::onScriptLog(const QString &msg)
{
    assert(QThread::currentThread() != thread_);
    GET_THREAD(thread)
    thread->channelWithMonkey()->sendCommand(PacketTypeForMonkey::ScriptLog,
                                             msg);
}

void Agent::saveScreenshots(const QString &path, int nSteps)
{
    DBGPRINT("%s: path '%s', nsteps %d", Q_FUNC_INFO, qPrintable(path), nSteps);
    auto lock = screenshots_.get();
    lock->first = path;
    lock->second = nSteps;
}
