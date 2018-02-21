//#define DEBUG_MOD_QTMONKEY
#include "qtmonkey.hpp"

#include <atomic>
#include <cassert>
#include <iostream>

#ifdef _WIN32 // windows both 32 bit and 64 bit
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

#include <QtCore/QCoreApplication>
#include <QtCore/QTextCodec>
#include <QtCore/QThread>

#include "common.hpp"
#include "json11.hpp"
#include "qtmonkey_app_api.hpp"

#ifdef DEBUG_MOD_QTMONKEY
#define DBGPRINT(fmt, ...) qDebug(fmt, __VA_ARGS__)
#else
#define DBGPRINT(fmt, ...)                                                     \
    do {                                                                       \
    } while (false)
#endif

using qt_monkey_agent::Private::PacketTypeForAgent;
using qt_monkey_agent::Private::Script;
using qt_monkey_app::Private::StdinReader;
using qt_monkey_app::QtMonkey;
using qt_monkey_common::operator<<;

namespace
{
static constexpr int waitBeforeExitMs = 300;

static inline std::ostream &operator<<(std::ostream &os, const QString &str)
{
    os << str.toUtf8();
    return os;
}

/*
win32 not allow overlapped I/O (see CreateFile [Consoles section]
plus QWinEventNotifier private on Qt 4.x and become public only on Qt 5.x
so just create thread for both win32 and posix
*/
class ReadStdinThread final : public QThread
{
public:
    ReadStdinThread(QObject *parent, StdinReader &reader);
    void run() override;
    void stop();

private:
    StdinReader &reader_;
    std::atomic<bool> timeToExit_{false};
#ifdef _WIN32
    HANDLE stdinHandle_;
#endif
};

#ifdef _WIN32
ReadStdinThread::ReadStdinThread(QObject *parent, StdinReader &reader)
    : QThread(parent), reader_(reader)
{
    stdinHandle_ = ::GetStdHandle(STD_INPUT_HANDLE);
    if (stdinHandle_ == INVALID_HANDLE_VALUE)
        throw std::runtime_error("GetStdHandle(STD_INPUT_HANDLE) return error: "
                                 + std::to_string(GetLastError()));
}

void ReadStdinThread::run()
{
    while (!timeToExit_) {
        char ch;

        switch (::GetFileType(stdinHandle_)) {
        case FILE_TYPE_CHAR: { // console
            DWORD numberOfEvents = 0;
            if (!::GetNumberOfConsoleInputEvents(stdinHandle_,
                                                 &numberOfEvents)) {
                reader_.emitError(
                    T_("Get number of bytes in stdin(console) failed: %1")
                        .arg(::GetLastError()));
                return;
            }
            if (numberOfEvents == 0)
                continue;
            INPUT_RECORD event;
            DWORD numberOfEventsRead = 0;
            if (!::ReadConsoleInput(stdinHandle_, &event, 1,
                                    &numberOfEventsRead)) {
                reader_.emitError(
                    T_("Reading from stdin error: %1").arg(::GetLastError()));
                return;
            }
            if (numberOfEventsRead != 1 || event.EventType != KEY_EVENT
                || !event.Event.KeyEvent.bKeyDown)
                continue;
            ch = event.Event.KeyEvent.uChar.AsciiChar;
            break;
        }
        case FILE_TYPE_PIPE: {
            DWORD bytesAvailInPipe;
            if (!PeekNamedPipe(stdinHandle_, nullptr, 0, nullptr,
                               &bytesAvailInPipe, nullptr)) {
                reader_.emitError(
                    T_("Get number of bytes in stdin(pipe) failed: %1")
                        .arg(::GetLastError()));
                return;
            }
            if (bytesAvailInPipe == 0)
                continue;
            if (timeToExit_)
                return;
        }
        // fall through
        default: {
            DWORD readBytes = 0;
            if (!::ReadFile(stdinHandle_, &ch, sizeof(ch), &readBytes,
                            nullptr)) {
                reader_.emitError(
                    T_("reading from stdin error: %1").arg(::GetLastError()));
                return;
            }
            if (readBytes == 0)
                return;
            break;
        }
        } // switch

        {
            auto ptr = reader_.data.get();
            ptr->append(ch);
        }
        reader_.emitDataReady();
    }
}

void ReadStdinThread::stop() { timeToExit_ = true; }
#else
ReadStdinThread::ReadStdinThread(QObject *parent, StdinReader &reader)
    : QThread(parent), reader_(reader)
{
}

void ReadStdinThread::run()
{
    while (!timeToExit_) {
        char ch;
        fd_set readfds, exceptfds;
        FD_ZERO(&readfds);
        FD_ZERO(&exceptfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(STDIN_FILENO, &exceptfds);
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000 * 100;
        const int selRes
            = select(STDIN_FILENO + 1, &readfds, nullptr, &exceptfds, &timeout);
        if (selRes == 0) { // timeout
            continue;
        } else if (selRes < 0) {
            reader_.emitError(T_("select stdin return error: %1").arg(errno));
            return;
        }
        if (FD_ISSET(STDIN_FILENO, &exceptfds)
            || !FD_ISSET(STDIN_FILENO, &readfds))
            continue;

        const ssize_t nBytes = ::read(STDIN_FILENO, &ch, sizeof(ch));
        if (nBytes < 0) {
            reader_.emitError(T_("reading from stdin error: %1").arg(errno));
            return;
        } else if (nBytes == 0) {
            break;
        }
        {
            auto ptr = reader_.data.get();
            ptr->append(ch);
        }
        reader_.emitDataReady();
    }
}

void ReadStdinThread::stop()
{
    timeToExit_ = true;
    do {
        if (::close(STDIN_FILENO) == 0)
            return;
    } while (errno == EINTR);
    throw std::runtime_error("close(stdin) failure: " + std::to_string(errno));
}
#endif
} // namespace {

QtMonkey::QtMonkey(bool exitOnScriptError)
    : exitOnScriptError_(exitOnScriptError)
{
    QProcessEnvironment curEnv = QProcessEnvironment::systemEnvironment();
    curEnv.insert(channelWithAgent_.requiredProcessEnvironment().first,
                  channelWithAgent_.requiredProcessEnvironment().second);
    userApp_.setProcessEnvironment(curEnv);

    connect(&userApp_, SIGNAL(error(QProcess::ProcessError)), this,
            SLOT(userAppError(QProcess::ProcessError)));
    connect(&userApp_, SIGNAL(finished(int, QProcess::ExitStatus)), this,
            SLOT(userAppFinished(int, QProcess::ExitStatus)));
    connect(&userApp_, SIGNAL(readyReadStandardOutput()), this,
            SLOT(userAppNewOutput()));
    connect(&userApp_, SIGNAL(readyReadStandardError()), this,
            SLOT(userAppNewErrOutput()));

    connect(&channelWithAgent_, SIGNAL(error(QString)), this,
            SLOT(communicationWithAgentError(const QString &)));
    connect(&channelWithAgent_, SIGNAL(newUserAppEvent(QString)), this,
            SLOT(onNewUserAppEvent(QString)));
    connect(&channelWithAgent_, SIGNAL(scriptError(QString)), this,
            SLOT(onScriptError(QString)));
    connect(&channelWithAgent_, SIGNAL(agentReadyToRunScript()), this,
            SLOT(onAgentReadyToRunScript()));
    connect(&channelWithAgent_, SIGNAL(scriptEnd()), this, SLOT(onScriptEnd()));
    connect(&channelWithAgent_, SIGNAL(scriptLog(QString)), this,
            SLOT(onScriptLog(QString)));

    readStdinThread_ = new ReadStdinThread(this, stdinReader_);
    stdinReader_.moveToThread(readStdinThread_);
    readStdinThread_->start();
    connect(&stdinReader_, SIGNAL(dataReady()), this, SLOT(stdinDataReady()));
}

QtMonkey::~QtMonkey()
{
    assert(readStdinThread_ != nullptr);
    auto thread = static_cast<ReadStdinThread *>(readStdinThread_);
    thread->stop();
    if (!thread->wait(1000 /*ms*/)) {
        qWarning("%s: thread still running", Q_FUNC_INFO);
        thread->terminate();
    }
    if (userApp_.state() != QProcess::NotRunning) {
        userApp_.terminate();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 3000 /*ms*/);
        userApp_.kill();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1000 /*ms*/);
    }
    // so any signals from channel will be disconected
    channelWithAgent_.close();
}

void QtMonkey::communicationWithAgentError(const QString &errStr)
{
    qWarning("%s: errStr %s", Q_FUNC_INFO, qPrintable(errStr));
}

void QtMonkey::onNewUserAppEvent(QString scriptLines)
{
    std::cout << qt_monkey_app::createPacketFromUserAppEvent(scriptLines)
              << std::endl;
}

void QtMonkey::userAppError(QProcess::ProcessError err)
{
    qDebug("%s: begin err %d", Q_FUNC_INFO, static_cast<int>(err));
    throw std::runtime_error(
        qPrintable(qt_monkey_common::processErrorToString(err)));
}

void QtMonkey::userAppFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug("%s: begin exitCode %d, exitStatus %d", Q_FUNC_INFO, exitCode,
           static_cast<int>(exitStatus));
    qt_monkey_common::processEventsFor(waitBeforeExitMs);
    if (exitCode != EXIT_SUCCESS)
        throw std::runtime_error(T_("user app exit status not %1: %2")
                                     .arg(EXIT_SUCCESS)
                                     .arg(exitCode)
                                     .toUtf8()
                                     .data());
    setScriptRunningState(false);
    if (toRunList_.empty()) {
        QCoreApplication::exit(EXIT_SUCCESS);
    } else {
        assert(!userAppPath_.isEmpty());
        restartDone_ = true;
        userApp_.start(userAppPath_, userAppArgs_);
    }
}

void QtMonkey::userAppNewOutput()
{
    const QString stdoutStr
        = QString::fromLocal8Bit(userApp_.readAllStandardOutput());
    std::cout << createPacketFromUserAppOutput(stdoutStr) << std::endl;
}

void QtMonkey::userAppNewErrOutput()
{
    const QString errOut
        = QString::fromLocal8Bit(userApp_.readAllStandardError());
    std::cout << createPacketFromUserAppErrors(errOut) << std::endl;
}

void QtMonkey::stdinDataReady()
{
    auto dataPtr = stdinReader_.data.get();
    size_t parserStopPos;
    parseOutputFromGui(
        {dataPtr->constData(), static_cast<size_t>(dataPtr->size())},
        parserStopPos,
        [this](QString script, QString scriptFileName) {
            auto scripts
                = Script::splitToExecutableParts(scriptFileName, script);
            for (auto &&script : scripts)
                toRunList_.push(std::move(script));
            onAgentReadyToRunScript();
        },
        [](QString errMsg) {
            std::cerr
                << T_("Can not parse gui<->monkey protocol: %1\n").arg(errMsg);
        });
    if (parserStopPos != 0)
        dataPtr->remove(0, parserStopPos);
}

void QtMonkey::onScriptError(QString errMsg)
{
    qDebug("%s: begin %s", Q_FUNC_INFO, qPrintable(errMsg));
    setScriptRunningState(false);
    std::cout << createPacketFromUserAppErrors(errMsg) << std::endl;
    if (exitOnScriptError_) {
        qt_monkey_common::processEventsFor(waitBeforeExitMs);
        throw std::runtime_error(
            T_("script return error: %1").arg(errMsg).toUtf8().data());
    }
}

bool QtMonkey::runScriptFromFile(QString codeToRunBeforeAll,
                                 QStringList scriptPathList,
                                 const char *encoding)
{
    if (encoding == nullptr)
        encoding = "UTF-8";

    for (const QString &fn : scriptPathList) {
        QFile f(fn);
        if (!f.open(QIODevice::ReadOnly)) {
            std::cerr << T_("Error: can not open %1\n").arg(fn);
            return false;
        }
        QTextStream t(&f);
        t.setCodec(QTextCodec::codecForName(encoding));
        const QString script = t.readAll();
        auto scripts = Script::splitToExecutableParts(fn, script);
        for (auto &&script : scripts) {
            if (!codeToRunBeforeAll.isEmpty()) {
                DBGPRINT("%s: we add code to run: '%s' to '%s'", Q_FUNC_INFO,
                         qPrintable(codeToRunBeforeAll), qPrintable(fn));
                Script prefs_script{QStringLiteral("<tmp>"), 1,
                                    codeToRunBeforeAll};
                prefs_script.setRunAfterAppStart(!toRunList_.empty());
                toRunList_.push(std::move(prefs_script));
            } else {
                script.setRunAfterAppStart(!toRunList_.empty());
            }
            toRunList_.push(std::move(script));
        }
    }

    return true;
}

void QtMonkey::onAgentReadyToRunScript()
{
    DBGPRINT("%s: begin is connected %s, run list empty %s, script running %s",
             Q_FUNC_INFO,
             channelWithAgent_.isConnectedState() ? "true" : "false",
             toRunList_.empty() ? "true" : "false",
             scriptRunning_ ? "true" : "false");
    if (!channelWithAgent_.isConnectedState() || toRunList_.empty()
        || scriptRunning_)
        return;

    if (toRunList_.front().runAfterAppStart()) {
        if (restartDone_) {
            restartDone_ = false;
        } else {
            DBGPRINT("%s: restartDone false, exiting", Q_FUNC_INFO);
            return;
        }
    }

    Script script = std::move(toRunList_.front());
    toRunList_.pop();
    QString code;
    script.releaseCode(code);
    channelWithAgent_.sendCommand(PacketTypeForAgent::SetScriptFileName,
                                  script.fileName());
    channelWithAgent_.sendCommand(PacketTypeForAgent::RunScript,
                                  std::move(code));
    setScriptRunningState(true);
}

void QtMonkey::onScriptEnd()
{
    setScriptRunningState(false);
    std::cout << createPacketFromScriptEnd() << std::endl;
}

void QtMonkey::onScriptLog(QString msg)
{
    std::cout << createPacketFromUserAppScriptLog(msg) << std::endl;
}

void QtMonkey::setScriptRunningState(bool val)
{
    scriptRunning_ = val;
    if (!scriptRunning_)
        onAgentReadyToRunScript();
}
