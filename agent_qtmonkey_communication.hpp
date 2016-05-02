#pragma once

#include <QtCore/QBasicTimer>
#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <cstdint>
#include <memory>

#include "shared_resource.hpp"

namespace qt_monkey_agent
{
namespace Private
{

class Script;

enum class PacketTypeForAgent : uint32_t {
    RunScript,
    // TODO: may be we need stuff bellow?
    SetBreakPointInScript,
    ContinueScript,
    HaltScript,
};

enum class PacketTypeForMonkey : uint32_t {
    NewUserAppEvent,
    ScriptError,
    ScriptEnd,
    ScriptLog,
    // TODO: may be need?
    ScriptStopOnBreakPoint,
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
private slots:
    void sendData();
    void readCommands();
    void connectionError(QAbstractSocket::SocketError);

private:
    QTcpSocket sock_;
    QBasicTimer timer_;
    SharedResource<QByteArray> sendBuf_;
    QByteArray recvBuf_;

    void timerEvent(QTimerEvent *) override;
};
}
}
