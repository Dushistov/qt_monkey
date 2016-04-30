#include "common.hpp"

#include <chrono>
#include <QtCore/QCoreApplication>

QString qt_monkey_common::processErrorToString(QProcess::ProcessError err)
{
    switch (err) {
    case QProcess::FailedToStart:
        return T_("Process failed to start");
    case QProcess::Crashed:
        return T_("Process crashed");
    default:
        return T_("Unknown error process error");
    }
}

void qt_monkey_common::processEventsFor(int timeoutMs)
{
    // QElapsedTimer occure only since Qt 4.7, so use chrono instead
    auto startTime = std::chrono::steady_clock::now();
    do {
        qApp->processEvents(QEventLoop::AllEvents, 10 /*ms*/);
    } while (std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - startTime)
             < std::chrono::milliseconds(timeoutMs));
}
