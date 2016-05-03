#pragma once

#include <functional>

class QScriptEngine;

namespace qt_monkey_agent
{
    /**
     * called before run of script, may be used to register custom types,
     * variables etc
     * @tparam QScriptEngine & script engine which is used to run monkey script
     */
    using PopulateScriptContext = std::function<void(QScriptEngine &)>;
}
