#pragma once

#include <cstdint>
#include <memory>

#include <QAtomicInteger>
#include <QtCore/QBasicTimer>
#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include "shared_resource.hpp"

namespace qt_monkey_agent
{
namespace Private
{

class Script;

enum class PacketTypeForAgent : uint32_t {
    RunScript,
    SetScriptFileName,
    // TODO: may be we need stuff bellow?
    SetBreakPointInScript,
    ContinueScript,
    HaltScript,
    CloseAck,
};

enum class PacketTypeForMonkey : uint32_t {
    NewUserAppEvent,
    ScriptError,
    ScriptEnd,
    ScriptLog,
    // TODO: may be need?
    ScriptStopOnBreakPoint,
    Close,
};

class CommunicationMonkeyPart
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
signals:
    void newUserAppEvent(QString);
    void scriptError(QString);
    void scriptEnd();
    void scriptLog(QString);
    void error(QString);
    void agentReadyToRunScript();

public:
    explicit CommunicationMonkeyPart(QObject *parent = nullptr);
    void sendCommand(PacketTypeForAgent pt, const QString &);
    bool isConnectedState() const;
    void close();
    const std::pair<QString, QString> &requiredProcessEnvironment() const
    {
        return envPrefs_;
    }
private slots:
    void handleNewConnection();
    void readDataFromClientSocket();
    void flushSendData();
    void clientDisconnected();
    void connectionError(QAbstractSocket::SocketError);

private:
    std::unique_ptr<QTcpServer> controlSock_;
    QTcpSocket *curClient_ = nullptr;
    QByteArray sendBuf_;
    QByteArray recvBuf_;
    std::pair<QString, QString> envPrefs_;
};

class CommunicationAgentPart
#ifndef Q_MOC_RUN
    final
#endif
    : public QObject
{
    Q_OBJECT
signals:
    void error(const QString &);
    void runScript(const qt_monkey_agent::Private::Script &);

public:
    explicit CommunicationAgentPart(QObject *parent = nullptr) : QObject(parent)
    {
    }
    void sendCommand(PacketTypeForMonkey pt, const QString &);
    bool connectToMonkey();
    void flushSendData();
    bool hasCloseAck() const;
    void clearCloseAck();
private slots:
    void sendData();
    void readCommands();
    void connectionError(QAbstractSocket::SocketError);

private:
    QTcpSocket sock_;
    QBasicTimer timer_;
    qt_monkey_common::SharedResource<QByteArray> sendBuf_;
    QByteArray recvBuf_;
    QString currentScriptFileName_;
    QAtomicInteger<int> close_ack_{0};

    void timerEvent(QTimerEvent *) override;
};
} // namespace Private
} // namespace qt_monkey_agent
