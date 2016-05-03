#include "script_ext.hpp"

#include <cassert>
#include <chrono>
#include <thread>

#include <QApplication>
#include <QtTest/QTest>

#include "my_custom_button.hpp"
#include "agent.hpp"
#include "script_api.hpp"

void ScriptExt::pressButton(const QString &caption)
{
    qDebug("%s: caption %s", Q_FUNC_INFO, qPrintable(caption));
    qt_monkey_agent::Agent *agent = qt_monkey_agent::Agent::instance();
    assert(agent != nullptr);
    qt_monkey_agent::ScriptAPI::Step step(*agent);
    int i;
    const int nAttempts = 30;
    MyCustomButton *btn = nullptr;
    for (i = 0; i < nAttempts; ++i) {
        const QString errMsg = agent->runCodeInGuiThreadSync([&btn, &caption] {
                for (QWidget *wdg : QApplication::allWidgets())
                    if (wdg->isVisible() && wdg->isEnabled()) {
                        btn = qobject_cast<MyCustomButton *>(wdg);
                        if (btn != nullptr && btn->text() == caption) {
                            return QString();
                        }
                    }
            
                return QString("Can not find button with such text %1").arg(caption);
            });
        if (errMsg.isEmpty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (i == nAttempts) {
        agent->throwScriptError(QString("Can not find button with such text %1").arg(caption));
        return;
    }
    assert(btn != nullptr);

    QString errMsg = agent->runCodeInGuiThreadSyncWithTimeout([btn, agent] {
            const QPoint clickPos = btn->rect().center();
            qt_monkey_agent::moveMouseTo(*agent, clickPos);
            QTest::mouseClick(btn, Qt::LeftButton, 0, clickPos);
            return QString();
        }, 5);
    if (!errMsg.isEmpty())
        agent->throwScriptError(std::move(errMsg));
    
}
