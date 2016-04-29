#include "script_api.hpp"

#include <cassert>
#include <QtCore/QStringList>

#include "agent.hpp"
#include "script_runner.hpp"

using qt_monkey_agent::ScriptAPI;

ScriptAPI::ScriptAPI(Agent &agent, QObject *parent): QObject(parent), agent_(agent)
{
}

void ScriptAPI::log(QString msgStr)
{
    agent_.sendToLog(std::move(msgStr));
}

void ScriptAPI::step()
{
    assert(!context()->backtrace().isEmpty());
    agent_.scriptCheckPoint(extractLineNumFromBacktraceLine(context()->backtrace().back()));
}

QWidget *getWidgetWithSuchName(const QString &objectName,
                               const int maxTimeToFindWidgetSec,
                               bool should_be_enable)
{
    qDebug("%s begin, search %s", Q_FUNC_INFO, qPrintable(objectName));
    QWidget *w = nullptr;
    static const int sleepTimeForWaitWidgetMs = 70;
#if 0
    const int max_attempts = (MAX_TIME_TO_FIND_WIDGET_SEC * 1000) / SLEEP_TIME_FOR_WAIT_WIDGET_MS + 1;
    for (int i = 0; i < max_attempts; ++i) {

        found_widget_ = nullptr;
        auto e = new qtmonkey_p::EmulFindWidget(object_name);
        qApp->postEvent(this->agent_, e);
        search_widget_sem_.acquire();

        w = found_widget_;
        found_widget_ = nullptr;
        if (w == nullptr || (should_be_enable && !(w->isVisible() && w->isEnabled())))
            qtmonkey::AgentThread::msleep(SLEEP_TIME_FOR_WAIT_WIDGET_MS);
        else {
            qDebug("%s: widget found", Q_FUNC_INFO);
            break;
        }
    }
#endif
    return w;
}

void ScriptAPI::mouseClick(QString widget, QString button, int x, int y)
{
    step();

}

void ScriptAPI::mouseDClick(QString widget, QString button, int x, int y)
{
}
