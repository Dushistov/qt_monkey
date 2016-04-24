#pragma once

#include <utility>
#include <functional>

class QWidget;
class QEvent;

namespace qt_monkey
{
/**
 * return not empty QString with javascript code if can handle this event
 * it is possible to return javascript comments to prevent javascript code
 * generation and wait another event, for example on key press return comment
 * and on key release return real code
 * @tparam QObject the same as in QObject::eventFilter
 * @tparam QEvent the same as in QObject::eventFilter
 * @tparam const std::pair<QWidget *, QString> & widget to which event is related, may be null, and it's name
 */
using CustomEventAnalyzer
= std::function<QString(QObject *, QEvent *, const std::pair<QWidget *, QString> &)>;
}
