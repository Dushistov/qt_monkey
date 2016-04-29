#include "script_api.hpp"

#include "agent.hpp"

using qt_monkey_agent::ScriptAPI;

ScriptAPI::ScriptAPI(Agent &agent, QObject *parent): QObject(parent), agent_(agent)
{
}

void ScriptAPI::log(QString msgStr)
{
    agent_.sendToLog(std::move(msgStr));
}

void ScriptAPI::mouseClick(QString widget, QString button, int x, int y)
{
}

void ScriptAPI::mouseDClick(QString widget, QString button, int x, int y)
{
}
