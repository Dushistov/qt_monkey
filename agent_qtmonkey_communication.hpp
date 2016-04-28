#pragma once

#include <QtCore/QBasicTimer>
#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <cstdint>

namespace qt_monkey_agent
{
namespace Private
{

class Script;

enum class PacketTypeForAgent : uint32_t {
    RunScript,
    // TODO: may be need?
    SetBreakPointInScript,
    ContinueScript,
    HaltScript,
};

enum class PacketTypeForMonkey : uint32_t {
    NewUserAppEvent,
    ScriptError,
    ScriptEnd,
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
    void error(const QString &);
    void agentReadyToRunScript();
public:
    explicit CommunicationMonkeyPart(QObject *parent = nullptr);
    void sendCommand(PacketTypeForAgent pt, const QString &);
private slots:
    void handleNewConnection();
    void readDataFromClientSocket();
    void flushSendData();
    void clientDisconnected();
    void connectionError(QAbstractSocket::SocketError);

private:
    QTcpServer controlSock_;
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
private slots:
    void sendData();
    void readCommands();
    void connectionError(QAbstractSocket::SocketError);

private:
    QTcpSocket sock_;
    QBasicTimer timer_;
    QByteArray sendBuf_;
    QByteArray recvBuf_;

    void timerEvent(QTimerEvent *) override;
};
}
}
