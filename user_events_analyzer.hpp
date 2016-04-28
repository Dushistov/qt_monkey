#pragma once

#include <QtCore/QObject>
#include <list>

#include "custom_event_analyzer.hpp"

namespace qt_monkey_agent
{
//@{
//! helper functions to implement custom event analyzers

//! Get full widget id, in form parent of parent id.parent id.widget id
extern QString fullQtWidgetId(const QWidget &w);

//! convert mouse button constant to string
extern QString mouseButtonEnumToString(Qt::MouseButton b);
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

public:
    explicit UserEventsAnalyzer(
        std::list<CustomEventAnalyzer> customEventAnalyzers,
        QObject *parent = nullptr);

private:
    std::list<CustomEventAnalyzer> customEventAnalyzers_;

    bool eventFilter(QObject *obj, QEvent *event) override;
    QString callCustomEventAnalyzers(QObject *obj, QEvent *event,
                                     const std::pair<QWidget *, QString> &widget) const;
};
}
