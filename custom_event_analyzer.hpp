#pragma once

#include <QtCore/QString>
#include <functional>
#include <utility>

class QWidget;
class QEvent;
class QObject;

namespace qt_monkey_agent
{
/**
 * Type of function which custom event analyzer
 * may save and reuse later for async code generation 
 */
using GenerateCommand = std::function<void(QString)>;

class Agent;

//! Event info for CustomEventAnalyzer
struct EventInfo final {
    Agent &agent;
    QObject *obj;//!< the same as in QObject::eventFilter
    QEvent *event;//!< the same as in QObject::eventFilter
    QWidget *widget;//!< widget to which related this event, may be null
    QString widgetName;//!< may contains cached value of name of obj
    const GenerateCommand &codeGenerator;//!< may be used by analyzer for async code generation
};

/**
 * return not empty string [QString] with javascript code if can handle this event
 * it is possible to return javascript comments to prevent javascript code
 * generation and wait another event, for example on key press return comment
 * and on key release return real code
 * @tparam const EventInfo & information about event
 */
using CustomEventAnalyzer = std::function<QString(const EventInfo &)>;
}
