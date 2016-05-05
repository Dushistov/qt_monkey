#include "qtmonkey.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QTextCodec>
#include <cstdio>
#ifdef _WIN32 // windows both 32 bit and 64 bit
#  include <windows.h>
#  include <QtCore/QWinEventNotifier>
#else
#  include <QtCore/QSocketNotifier>
#endif

#include "common.hpp"
#include "qtmonkey_app_api.hpp"

using qt_monkey_app::QtMonkey;
using qt_monkey_agent::Private::Script;
using qt_monkey_agent::Private::PacketTypeForAgent;

static constexpr int waitBeforeExitMs = 300;

QtMonkey::QtMonkey(bool exitOnScriptError)
    : exitOnScriptError_(exitOnScriptError)
{
    if (!stdout_.open(stdout, QIODevice::WriteOnly, QFile::DontCloseHandle)
        || !stderr_.open(stderr, QIODevice::WriteOnly, QFile::DontCloseHandle))
        throw std::runtime_error("File -> QFile failed");
    cout_.setDevice(&stdout_);
    cerr_.setDevice(&stderr_);
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

    if (std::setvbuf(stdin, nullptr, _IONBF, 0))
        throw std::runtime_error("setvbuf failed");
#ifdef _WIN32
    HANDLE stdinHandle = ::GetStdHandle(STD_INPUT_HANDLE);
    if (stdinHandle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("GetStdHandle(STD_INPUT_HANDLE) return error");
    auto stdinNotifier = new QWinEventNotifier(stdinHandle, this);
    connect(stdinNotifier, SIGNAL(activated(HANDLE)), this,
            SLOT(stdinDataReady()));
#else
    int stdinHandler = ::fileno(stdin);
    if (stdinHandler < 0)
        throw std::runtime_error("fileno(stdin) return error");
    auto stdinNotifier
        = new QSocketNotifier(stdinHandler, QSocketNotifier::Read, this);
    connect(stdinNotifier, SIGNAL(activated(int)), this,
            SLOT(stdinDataReady()));
#endif
}

QtMonkey::~QtMonkey()
{
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
    cout_ << qt_monkey_app::createPacketFromUserAppEvent(scriptLines) << "\n";
    cout_.flush();
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
    QCoreApplication::exit(EXIT_SUCCESS);
}

void QtMonkey::userAppNewOutput()
{
    // just ignore for now
    userApp_.readAllStandardOutput();
}

void QtMonkey::userAppNewErrOutput()
{
    const QString errOut
        = QString::fromLocal8Bit(userApp_.readAllStandardError());
    cout_ << createPacketFromUserAppErrors(errOut) << "\n";
    cout_.flush();
}

void QtMonkey::stdinDataReady()
{
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n')
        stdinBuf_ += static_cast<unsigned char>(ch);
    size_t parserStopPos;
    parseOutputFromGui(
        stdinBuf_, parserStopPos,
        [this](QString script, QString scriptFileName) {
            toRunList_.push(Script{std::move(script)});
            onAgentReadyToRunScript();
        },
        [this](QString errMsg) {
            cerr_
                << T_("Can not parse gui<->monkey protocol: %1\n").arg(errMsg);
        });
    if (parserStopPos != 0)
        stdinBuf_.remove(0, parserStopPos);
}

void QtMonkey::onScriptError(QString errMsg)
{
    qDebug("%s: begin %s", Q_FUNC_INFO, qPrintable(errMsg));
    setScriptRunningState(false);
    cout_ << createPacketFromUserAppErrors(errMsg) << "\n";
    cout_.flush();
    if (exitOnScriptError_) {
        qt_monkey_common::processEventsFor(waitBeforeExitMs);
        throw std::runtime_error(T_("script return error: %1")
                                 .arg(errMsg).toUtf8().data());
    }
}

bool QtMonkey::runScriptFromFile(QStringList scriptPathList,
                                 const char *encoding)
{
    if (encoding == nullptr)
        encoding = "UTF-8";

    QString script;

    for (const QString &fn : scriptPathList) {
        QFile f(fn);
        if (!f.open(QIODevice::ReadOnly)) {
            cerr_ << T_("Error: can not open %1\n").arg(fn);
            return false;
        }
        QTextStream t(&f);
        t.setCodec(QTextCodec::codecForName(encoding));
        if (!script.isEmpty())
            script += "\n<<<RESTART FROM HERE>>>\n";
        script += t.readAll();
    }

    toRunList_.push(Script{std::move(script)});

    return true;
}

void QtMonkey::onAgentReadyToRunScript()
{
    qDebug("%s: begin", Q_FUNC_INFO);

    if (!channelWithAgent_.isConnectedState() || toRunList_.empty()
        || scriptRunning_)
        return;

    Script script = std::move(toRunList_.front());
    toRunList_.pop();
    QString code;
    script.releaseCode(code);
    channelWithAgent_.sendCommand(PacketTypeForAgent::RunScript,
                                  std::move(code));
    setScriptRunningState(true);
}

void QtMonkey::onScriptEnd()
{
    setScriptRunningState(false);
    cout_ << createPacketFromScriptEnd() << "\n";
    cout_.flush();
}

void QtMonkey::onScriptLog(QString msg)
{
    cout_ << createPacketFromUserAppScriptLog(msg) << "\n";
    cout_.flush();
}

void QtMonkey::setScriptRunningState(bool val)
{
    scriptRunning_ = val;
    if (!scriptRunning_)
        onAgentReadyToRunScript();
}
