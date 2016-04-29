#include "qtmonkey.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QSocketNotifier>
#include <QtCore/QTextCodec>
#include <cstdio>

#include "common.hpp"
#include "json11.hpp"
#include "qtmonkey_app_api.hpp"

using json11::Json;
using qt_monkey_app::QtMonkey;
using qt_monkey_agent::Private::Script;
using qt_monkey_agent::Private::PacketTypeForAgent;

QtMonkey::QtMonkey()
    : cout_{stdout}, cerr_{stderr}
{
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
    connect(&channelWithAgent_, SIGNAL(scriptLog(QString)), this, SLOT(onScriptLog(QString)));

    if (std::setvbuf(stdin, nullptr, _IONBF, 0))
        throw std::runtime_error("setvbuf failed");
#ifdef _WIN32 // windows both 32 bit and 64 bit
    HANDLE stdinHandle = ::GetStdHandle(STD_INPUT_HANDLE);
    if (stdinHandle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("GetStdHandle(STD_INPUT_HANDLE) return error");
    auto stdinNotifier = new QWinEventNotifier(stdinHandle, this);
    connect(stdinHandle, SIGNAL(activated(HANDLE)), this,
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
        QCoreApplication::processEvents(QEventLoop::AllEvents, 3000/*ms*/);
        userApp_.kill();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1000/*ms*/);
    }
}

void QtMonkey::communicationWithAgentError(const QString &errStr)
{
    qWarning("%s: errStr %s", Q_FUNC_INFO, qPrintable(errStr));
}

void QtMonkey::onNewUserAppEvent(QString scriptLines)
{
    cout_ << qt_monkey_app::createPacketFromUserAppEvent(scriptLines)
          << "\n";
    cout_.flush();
}

void QtMonkey::userAppError(QProcess::ProcessError err)
{
    qDebug("%s: begin err %d", Q_FUNC_INFO, static_cast<int>(err));
    throw std::runtime_error(qPrintable(processErrorToString(err)));
}

void QtMonkey::userAppFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug("%s: begin exitCode %d, exitStatus %d", Q_FUNC_INFO, exitCode,
           static_cast<int>(exitStatus));
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
    std::string::size_type parserStopPos;
    std::string err;
    auto json = Json::parse_multi(stdinBuf_, parserStopPos, err);
    if (parserStopPos != 0)
        stdinBuf_.erase(0, parserStopPos);
    qDebug("%s: we are here!!!", Q_FUNC_INFO);
}

void QtMonkey::onScriptError(QString errMsg)
{
    cout_ << createPacketFromUserAppErrors(errMsg) << "\n";
    cout_.flush();
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

    if (toRunList_.empty())
        return;

    Script script = std::move(toRunList_.front());
    toRunList_.pop();
    QString code;
    script.releaseCode(code);
    channelWithAgent_.sendCommand(PacketTypeForAgent::RunScript, std::move(code));
}

void QtMonkey::onScriptEnd()
{
    cout_ << createPacketFromScriptEnd() << "\n";
    cout_.flush();
}

void QtMonkey::onScriptLog(QString msg)
{
    cout_ << createPacketFromUserAppScriptLog(msg) << "\n";
    cout_.flush();
}
