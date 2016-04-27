#include "qtmonkey.hpp"

#include <cstdio>
#include <QtCore/QCoreApplication>
#include <QtCore/QSocketNotifier>

#include "common.hpp"
#include "json11.hpp"

using json11::Json;

namespace
{
struct QStringJsonTrait final
{
    explicit QStringJsonTrait(QString &s): str_(s) {}
    std::string to_json() const { return str_.toStdString(); }
private:
    QString &str_;
};
}

QtMonkey::QtMonkey(QString userAppPath, QStringList userAppArgs): cout_{stdout}, cerr_{stderr}
{
    connect(&userApp_, SIGNAL(error(QProcess::ProcessError)), this,
            SLOT(userAppError(QProcess::ProcessError)));
    connect(&userApp_, SIGNAL(finished(int, QProcess::ExitStatus)), this,
            SLOT(userAppFinished(int, QProcess::ExitStatus)));
    connect(&userApp_, SIGNAL(readyReadStandardOutput()), this,
            SLOT(userAppNewOutput()));
    connect(&userApp_, SIGNAL(readyReadStandardError()), this,
            SLOT(userAppNewErrOutput()));

    connect(&channelWithAgent_, SIGNAL(error(const QString&)), this, SLOT(communicationWithAgentError(const QString&)));
    connect(&channelWithAgent_, SIGNAL(newUserAppEvent(QString)), this, SLOT(onNewUserAppEvent(QString)));
    userApp_.start(userAppPath, userAppArgs);

    if (std::setvbuf(stdin, nullptr, _IONBF, 0))
        throw std::runtime_error("setvbuf failed");
#ifdef _WIN32//windows both 32 bit and 64 bit
    HANDLE stdinHandle = ::GetStdHandle(STD_INPUT_HANDLE);
    if (stdinHandle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("GetStdHandle(STD_INPUT_HANDLE) return error");
    auto stdinNotifier = new QWinEventNotifier(stdinHandle, this);
    connect(stdinHandle, SIGNAL(activated(HANDLE)), this, SLOT(stdinDataReady()));
#else
    int stdinHandler = ::fileno(stdin);
    if (stdinHandler < 0)
        throw std::runtime_error("fileno(stdin) return error");
    auto stdinNotifier = new QSocketNotifier(stdinHandler, QSocketNotifier::Read, this);
    connect(stdinNotifier, SIGNAL(activated(int)), this, SLOT(stdinDataReady()));
#endif
}

void QtMonkey::communicationWithAgentError(const QString &errStr)
{
    qWarning("%s: errStr %s", Q_FUNC_INFO, qPrintable(errStr));
}

void QtMonkey::onNewUserAppEvent(QString scriptLines)
{
    auto json = Json::object{
        {"event", Json::object{{"script", QStringJsonTrait{scriptLines}}}}
    };
    cout_ << QString::fromStdString(Json{json}.dump()) << "\n";
    cout_.flush();
}

void QtMonkey::userAppError(QProcess::ProcessError err)
{
    qDebug("%s: begin err %d", Q_FUNC_INFO, static_cast<int>(err));
    throw std::runtime_error(qPrintable(processErrorToString(err)));
}

void QtMonkey::userAppFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug("%s: begin exitCode %d, exitStatus %d", Q_FUNC_INFO, exitCode, static_cast<int>(exitStatus));
    if (exitCode != EXIT_SUCCESS)
        throw std::runtime_error(T_("user app exit status not %1: %2").arg(EXIT_SUCCESS).arg(exitCode).toUtf8().data());
    QCoreApplication::exit(EXIT_SUCCESS);
}

void QtMonkey::userAppNewOutput()
{
}

void QtMonkey::userAppNewErrOutput()
{
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
