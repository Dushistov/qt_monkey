#include "agent.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <atomic>
#include <cassert>
#include <functional>

#include "agent_qtmonkey_communication.hpp"
#include "common.hpp"
#include "script.hpp"
#include "script_api.hpp"
#include "script_runner.hpp"
#include "user_events_analyzer.hpp"

using namespace qt_monkey_agent;
using namespace qt_monkey_agent::Private;

#define GET_THREAD(__name__)                                                   \
    auto __name__ = static_cast<AgentThread *>(thread_);                       \
    if (__name__->isFinished()) {                                              \
        qWarning("%s: thread is finished", Q_FUNC_INFO);                       \
        return;                                                                \
    }

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
    bool event(QEvent *event) override
    {
        if (event->type() != eventType_)
            return false;

        static_cast<FuncEvent *>(event)->exec();

        return true;
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

Agent::Agent(std::list<CustomEventAnalyzer> customEventAnalyzers)
    : eventAnalyzer_(
          new UserEventsAnalyzer(std::move(customEventAnalyzers), this))
{
    // make sure that type is referenced
    qMetaTypeId<qt_monkey_agent::Private::Script>();

    connect(eventAnalyzer_, SIGNAL(userEventInScriptForm(const QString &)),
            this, SLOT(onUserEventInScriptForm(const QString &)));
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

    thread->runInThread([this, thread] {
        thread->channelWithMonkey()->flushSendData();
    });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1000/*ms*/);
    thread->quit();
    thread->wait();
}

void Agent::onUserEventInScriptForm(const QString &script)
{
    GET_THREAD(thread)
    thread->runInThread([thread, script] {
        thread->channelWithMonkey()->sendCommand(
            PacketTypeForMonkey::NewUserAppEvent, script);
    });
}

void Agent::onRunScriptCommand(const Private::Script &script)
{
    GET_THREAD(thread)
    assert(QThread::currentThread() == thread_);
    qDebug("%s: run script", Q_FUNC_INFO);
    ScriptAPI api{*this};
    ScriptRunner sr{api};
    curScriptRunner_ = &sr;
    QString errMsg;
    sr.runScript(script, errMsg);    
    curScriptRunner_ = nullptr;
    if (!errMsg.isEmpty()) {
        qWarning("AGENT: %s: script return error", Q_FUNC_INFO);
        thread->channelWithMonkey()->sendCommand(
            PacketTypeForMonkey::ScriptError, errMsg);
    }
    thread->channelWithMonkey()->sendCommand(
        PacketTypeForMonkey::ScriptEnd, QString());
}

void Agent::sendToLog(QString msg)
{
    qDebug("%s: msg %s", Q_FUNC_INFO, qPrintable(msg));
    GET_THREAD(thread)
    thread->channelWithMonkey()->sendCommand(PacketTypeForMonkey::ScriptLog,
                                             std::move(msg));
}

void Agent::scriptCheckPoint()
{
    assert(QThread::currentThread() == thread_);
    assert(curScriptRunner_ != nullptr);
    const int lineno = curScriptRunner_->currentLineNum();
    qDebug("%s: lineno %d", Q_FUNC_INFO, lineno);
}
