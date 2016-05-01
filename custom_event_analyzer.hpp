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

/**
 * return not empty QString with javascript code if can handle this event
 * it is possible to return javascript comments to prevent javascript code
 * generation and wait another event, for example on key press return comment
 * and on key release return real code
 * @tparam QObject the same as in QObject::eventFilter
 * @tparam QEvent the same as in QObject::eventFilter
 * @tparam const std::pair<QWidget *, QString> & widget to which event is
 * related, may be null, and it's name
 * @tparam const GenerateCommand & may be used by analyzer for async code generation
 */
using CustomEventAnalyzer = std::function<QString(
    QObject *, QEvent *, const std::pair<QWidget *, QString> &,
    const GenerateCommand &)>;
}
