#pragma once

#include <QtCore/QObject>
#include <QtScript/QScriptable>

class QPoint;

namespace qt_monkey_agent
{

class Agent;

/**
 * public slots of this class are functions
 * that exposed to qt monkey script
 */
class ScriptAPI
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject,
      private QScriptable
{
    Q_OBJECT
public:
    explicit ScriptAPI(Agent &agent, QObject *parent = nullptr);
public slots:
    //@{
    /**
* Emulate click or double click on widget
     * @param widget name of widget
     * @param button mouse button
     * @param x x of click in widget coordinate system
     * @param y y of click in widget coordinate system
     */
    void mouseClick(QString widget, QString button, int x, int y);
    void mouseDClick(QString widget, QString button, int x, int y);
    //@}
    /**
     * send message to log
     * @param msgStr string with message
     */
    void log(QString msgStr);

    /**
     * How many time to wait QWidget appearing before give up
     * @param v timeout in seconds
     */
    void setWaitWidgetAppearingTimeoutSec(int v)
    {
        waitWidgetAppearTimeoutSec_ = v;
    }
    int getWaitWidgetAppearingTimeoutSec() const
    {
        return waitWidgetAppearTimeoutSec_;
    }

    /**
     * how many time we wait in case that click cause new QEventLoop
     * createion, for example QDialog in modal mode
     * @param v timeout in seconds
     */
    void setNewEventLoopWaitTimeout(int v) { newEventLoopWaitTimeoutSecs_ = v; }

private:
    Agent &agent_;
    int waitWidgetAppearTimeoutSec_ = 30;
    int newEventLoopWaitTimeoutSecs_ = 5;

    void doMouseClick(const QString &widgetName, const QString &buttonName,
                      int x, int y, bool doubleClick);
    void clickInGuiThread(const QPoint &posA, QWidget &wA, Qt::MouseButton btn, bool dblClick);
    void moveMouseTo(const QPoint &);
};
}
