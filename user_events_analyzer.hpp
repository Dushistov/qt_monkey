#pragma once

#include <QtCore/QObject>
#include <list>
#include <set>

#include "custom_event_analyzer.hpp"

class QTreeWidget;
class QTreeWidgetItem;

namespace qt_monkey_agent
{
//@{
//! helper functions to implement custom event analyzers

//! Get full widget id, in form parent of parent id.parent id.widget id
QString fullQtWidgetId(const QWidget &w);

//! convert mouse button constant <-> string
QString mouseButtonEnumToString(Qt::MouseButton b);
bool stringToMouseButton(const QString &str, Qt::MouseButton &bt);
//@}

//! helper class to work with QTreeWidgetItem
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
private slots:
    void itemExpanded(QTreeWidgetItem *);
    void treeWidgetDestroyed(QObject *);

private:
    const GenerateCommand &generateScriptCmd_;
    std::set<QObject *> treeWidgetsSet_;
};

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

public:
    explicit UserEventsAnalyzer(
        std::list<CustomEventAnalyzer> customEventAnalyzers,
        QObject *parent = nullptr);

private:
    std::list<CustomEventAnalyzer> customEventAnalyzers_;
    const GenerateCommand generateScriptCmd_;

    bool eventFilter(QObject *obj, QEvent *event) override;
    QString
    callCustomEventAnalyzers(QObject *obj, QEvent *event,
                             const std::pair<QWidget *, QString> &widget) const;
};
}
