#pragma once

#include <list>
#include <map>
#include <set>

#include <QKeySequence>
#include <QPoint>
#include <QtCore/QDateTime>
#include <QtCore/QEvent>
#include <QtCore/QObject>

#include "custom_event_analyzer.hpp"

class QTreeWidget;
class QTreeWidgetItem;
class QKeyEvent;
class QMouseEvent;
class QTreeView;
class QModelIndex;
class QAbstractItemModel;
class QAction;

namespace qt_monkey_agent
{
class Agent;

//@{
//! helper functions to implement custom event analyzers

//! Get full widget id, in form parent of parent id.parent id.widget id
QString fullQtWidgetId(const QWidget &w);

//! convert mouse button constant <-> string
QString mouseButtonEnumToString(Qt::MouseButton b);
bool stringToMouseButton(const QString &str, Qt::MouseButton &bt);
//@}

/**
 * Analyzer user event and genearte based of them javascript code
 */
class UserEventsAnalyzer
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
signals:
    void userEventInScriptForm(const QString &);
    void scriptLog(const QString &);

public:
    UserEventsAnalyzer(Agent &agent, const QKeySequence &showObjectShortCut,
                       std::list<CustomEventAnalyzer> customEventAnalyzers,
                       QObject *parent = nullptr);

private:
    Agent &agent_;
    struct {
        QEvent::Type type = QEvent::None;
        QDateTime timestamp;
        int key = -1;
    } lastKeyEvent_;
    struct {
        QEvent::Type type = QEvent::None;
        QDateTime timestamp;
        QPoint globalPos;
        Qt::MouseButtons buttons;
        QString widgetName;
    } lastMouseEvent_;
    size_t keyPress_ = 0;
    size_t keyRelease_ = 0;
    std::list<CustomEventAnalyzer> customEventAnalyzers_;
    const GenerateCommand generateScriptCmd_;
    const QKeySequence showObjectShortCut_;

    bool eventFilter(QObject *obj, QEvent *event) override;
    QString
    callCustomEventAnalyzers(QObject *obj, QEvent *event, QWidget *widget, const QString &widgetName) const;
    bool alreadySawSuchKeyEvent(QKeyEvent *keyEvent);
    bool alreadySawSuchMouseEvent(const QString &widgetName,
                                  QMouseEvent *mouseEvent);
};

namespace Private
{
//@{
//! helper classes to catch signals of widgets
//! \todo remove when we drop support of Qt 4.x

class TreeWidgetWatcher
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
public:
    TreeWidgetWatcher(const GenerateCommand &generateScriptCmd,
                      QObject *parent = nullptr)
        : QObject(parent), generateScriptCmd_(generateScriptCmd)
    {
    }
    //! \return false if already watched
    bool watch(QTreeWidget *tw);
    void disconnectAll();
private slots:
    void itemExpanded(QTreeWidgetItem *);
    void treeWidgetDestroyed(QObject *);

private:
    const GenerateCommand &generateScriptCmd_;
    std::set<QObject *> treeWidgetsSet_;
};

class TreeViewWatcher
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
public:
    TreeViewWatcher(const GenerateCommand &generateScriptCmd,
                    QObject *parent = nullptr)
        : QObject(parent), generateScriptCmd_(generateScriptCmd)
    {
    }
    void watch(QTreeView &tw);
    void disconnectAll();
private slots:
    void treeViewExpanded(const QModelIndex &index);
    void treeViewDestroyed(QObject *obj);

private:
    const GenerateCommand &generateScriptCmd_;
    std::set<const QObject *> treeViewSet_;
    std::map<const QAbstractItemModel *, const QObject *> modelToView_;
};

class MacMenuActionWatcher
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
public:
    MacMenuActionWatcher(const GenerateCommand &generateScriptCmd,
                         QObject *parent = nullptr)
             : QObject(parent), generateScriptCmd_(generateScriptCmd)
    {
    }
    void startWatch(Agent &agent, QAction &act, QString menuName);
    void stopWatch(Agent &agent, QAction &act);
private slots:
    void onTriggered();
private:
    const GenerateCommand &generateScriptCmd_;
    std::map<QAction *, QString> actions_;
};
//@}
}//namespace Private
}
