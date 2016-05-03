#pragma once

#include <QtCore/QObject>
#include <QtScript/QScriptable>

class QPoint;
class QAbstractItemView;

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
    class Step final {
    public:
        explicit Step(Agent &agent);
        ~Step();
    };
    explicit ScriptAPI(Agent &agent, QObject *parent = nullptr);
public slots:
    /**
      * send message to log
      * @param msgStr string with message
      */
    void log(const QString &msgStr);

    //@{
    /**
     * Emulate click or double click on widget
     * @param widget name of widget
     * @param button mouse button
     * @param x x of click in widget coordinate system
     * @param y y of click in widget coordinate system
     */
    void mouseClick(const QString &widget, const QString &button, int x, int y);
    void mouseDClick(const QString &widget, const QString &button, int x, int y);
    //@}
    //@{
    /**
     * Group of functions to emulate activate item (menu item, list item etc)
     * @param widget name of widget who owns element
     * @param actionName text content of element (caption on element)
     * @param searchFlags search criteria of element, see Qt::MatchFlag
     */
    void activateItem(const QString &widget, const QString &actionName);
    void activateItem(const QString &widget, const QString &actionName,
                      const QString &searchFlags);
    //@}
    /**
     * Activate element using as identifier of element pair of indexes
     * number of row and number of column
     */
    void activateItemInView(const QString &widget, const QList<QVariant> &indexesList);

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

    //@{
    /**
     * Expand subtree in QTreeWidget and QTreeView
     * @param treeName widget name
     * @param item caption of subtree or index in QTreeView
     */
    void expandItemInTree(const QString &treeName, const QString &item);
    void expandItemInTreeView(const QString &treeName, const QList<QVariant> &item);
    //@}
    /**
     * sleep some time (in help thread, main gui thread works at this time)
     * @param ms amount of milliseconds to sleep
     */
    void Wait(int ms);
private:
    Agent &agent_;
    int waitWidgetAppearTimeoutSec_ = 30;
    int newEventLoopWaitTimeoutSecs_ = 5;

    void doMouseClick(const QString &widgetName, const QString &buttonName,
                      int x, int y, bool doubleClick);
    void clickInGuiThread(const QPoint &posA, QWidget &wA, Qt::MouseButton btn,
                          bool dblClick);
    void moveMouseTo(const QPoint &);
    void doClickItem(const QString &objectName, const QString &itemName,
                     bool isDblClick,
                     Qt::MatchFlag searchItemFlag = Qt::MatchStartsWith);
    QString activateItemInGuiThread(QWidget *w, const QString &itemName,
                                    bool dblClick, Qt::MatchFlag matchFlag);
    QString clickOnItemInGuiThread(const QList<int> &idxPos,
                                QAbstractItemView *view, bool dblClick);
};
}
