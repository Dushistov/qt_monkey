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
enum class PacketTypeForAgent : uint32_t {
    // TODO: may be need?
    RunScript,
    SetBreakPointInScript,
    ContinueScript,
    HaltScript,
};

enum class PacketTypeForMonkey : uint32_t {
    NewUserAppEvent,
    ScriptError,
    // TODO: may be need?
    ScriptEnd,
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
    void error(const QString &);

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

    void timerEvent(QTimerEvent *) override;
};
}
}
