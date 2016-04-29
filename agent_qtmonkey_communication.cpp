#include "agent_qtmonkey_communication.hpp"

#include <QtCore/QDataStream>
#include <QtCore/QThread>
#include <QtCore/QTimerEvent>
#include <cassert>
#include <cstring>
#include <type_traits>

#include "common.hpp"
#include "script.hpp"

using namespace qt_monkey_agent::Private;

static const char QTMONKEY_PORT_ENV_NAME[] = "QTMONKEY_PORT";

namespace
{

static const uint32_t magicNumber = 0x12345678u;

enum class PacketState { Damaged, NotReady, Ready };

static PacketState calcPacketState(const QByteArray &buf)
{
    if (static_cast<size_t>(buf.size()) < sizeof(magicNumber))
        return PacketState::NotReady;
    std::remove_const<decltype(magicNumber)>::type curMagicNumber;
    std::memcpy(&curMagicNumber, buf.constData(), sizeof(curMagicNumber));
    if (curMagicNumber != magicNumber)
        return PacketState::Damaged;
    uint32_t packetType;
    uint32_t packetSize;
    const size_t headerSize
        = sizeof(magicNumber) + sizeof(packetType) + sizeof(packetSize);
    if (static_cast<size_t>(buf.size()) < headerSize)
        return PacketState::NotReady;
    std::memcpy(&packetSize,
                buf.constData() + sizeof(magicNumber) + sizeof(packetType),
                sizeof(packetSize));
    if (packetSize > (1024 * 1024))
        return PacketState::Damaged;
    if (static_cast<size_t>(buf.size()) < (packetSize + headerSize))
        return PacketState::NotReady;
    return PacketState::Ready;
}

static QByteArray createPacket(uint32_t packetType, const QString &text)
{
    QByteArray res;
    uint32_t packetSize;
    const size_t headerSize
        = sizeof(magicNumber) + sizeof(packetType) + sizeof(packetSize);
    res.reserve(headerSize + text.length());
    res.resize(headerSize);
    res.append(text.toUtf8());
    packetSize = res.size() - headerSize;
    std::memcpy(res.data(), &magicNumber, sizeof(magicNumber));
    std::memcpy(res.data() + sizeof(magicNumber), &packetType,
                sizeof(packetType));
    std::memcpy(res.data() + sizeof(magicNumber) + sizeof(packetType),
                &packetSize, sizeof(packetSize));
    return res;
}

static std::pair<uint32_t, QString> extractFromPacket(QByteArray &buf)
{
    assert(calcPacketState(buf) == PacketState::Ready);
    uint32_t packetType;
    std::memcpy(&packetType, buf.constData() + sizeof(magicNumber),
                sizeof(packetType));
    uint32_t packetSize;
    std::memcpy(&packetSize,
                buf.constData() + sizeof(magicNumber) + sizeof(packetType),
                sizeof(packetSize));
    const size_t headerSize
        = sizeof(magicNumber) + sizeof(packetType) + sizeof(packetSize);
    assert((packetSize + headerSize) <= static_cast<size_t>(buf.size()));
    std::pair<uint32_t, QString> res{
        packetType,
        QString::fromUtf8(buf.constData() + headerSize, packetSize)};
    buf.remove(0, headerSize + packetSize);
    return res;
}
}

CommunicationMonkeyPart::CommunicationMonkeyPart(QObject *parent)
    : QObject(parent)
{
    connect(&controlSock_, SIGNAL(newConnection()), this,
            SLOT(handleNewConnection()));
    if (!controlSock_.listen(QHostAddress::LocalHost))
        throw std::runtime_error(
            qPrintable(T_("start listen of tcp socket failed")));
    QByteArray portNum;
    QDataStream stream(&portNum, QIODevice::WriteOnly);
    stream << controlSock_.serverPort();
    qDebug("%s: we listen %d\n", Q_FUNC_INFO,
           static_cast<int>(controlSock_.serverPort()));
    if (!qputenv(QTMONKEY_PORT_ENV_NAME, portNum))
        throw std::runtime_error(qPrintable(T_("can not set env variable")));
}

void CommunicationMonkeyPart::handleNewConnection()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    curClient_ = controlSock_.nextPendingConnection();
    connect(curClient_, SIGNAL(readyRead()), this,
            SLOT(readDataFromClientSocket()));
    connect(curClient_, SIGNAL(bytesWritten(qint64)), this,
            SLOT(flushSendData()));
    connect(curClient_, SIGNAL(disconnected()), this,
            SLOT(clientDisconnected()));
    connect(curClient_, SIGNAL(error(QAbstractSocket::SocketError)), this,
            SLOT(connectionError(QAbstractSocket::SocketError)));
    emit agentReadyToRunScript();
}

void CommunicationMonkeyPart::readDataFromClientSocket()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    assert(curClient_ != nullptr);
    if (curClient_ == nullptr)
        return;
    const qint64 nBytes = curClient_->bytesAvailable();
    if (nBytes <= 0) {
        qWarning("%s: no data availabile: %lld\n", Q_FUNC_INFO,
                 static_cast<long long>(nBytes));
        return;
    }
    const auto wasSize = recvBuf_.size();
    recvBuf_.resize(wasSize + nBytes);
    const qint64 readBytes
        = curClient_->read(recvBuf_.data() + wasSize, nBytes);
    if (readBytes <= 0) {
        qWarning("%s: read data error", Q_FUNC_INFO);
        recvBuf_.resize(wasSize);
        emit error(T_("Can not read data from client"));
        return;
    }
    if (nBytes != readBytes)
        recvBuf_.resize(wasSize + readBytes);
    switch (calcPacketState(recvBuf_)) {
    case PacketState::Damaged:
        qWarning("%s: packet damaged", Q_FUNC_INFO);
        emit error(T_("packet from qmonkey's agent damaged"));
        break;
    case PacketState::NotReady:
        /*nothing*/ break;
    case PacketState::Ready: {
        auto packet = extractFromPacket(recvBuf_);
        switch (static_cast<PacketTypeForMonkey>(packet.first)) {
        case PacketTypeForMonkey::NewUserAppEvent:
            qDebug("%s: user app event: '%s'", Q_FUNC_INFO,
                   qPrintable(packet.second));
            emit newUserAppEvent(std::move(packet.second));
            break;
        case PacketTypeForMonkey::ScriptError:
            emit scriptError(std::move(packet.second));
            break;
        case PacketTypeForMonkey::ScriptEnd:
            emit scriptEnd();
            break;
        case PacketTypeForMonkey::ScriptLog:
            emit scriptLog(std::move(packet.second));
            break;
        default:
            qWarning("%s: unknown type of packet from qtmonkey's agent: %u",
                     Q_FUNC_INFO, static_cast<unsigned>(packet.first));
            emit error(T_("unknown type of packet from qtmonkey's agent"));
            break;
        }
    }
    }
}

void CommunicationMonkeyPart::flushSendData()
{
    if (!sendBuf_.isEmpty()) {
        assert(curClient_ != nullptr);
        qint64 writen = curClient_->write(sendBuf_);
        if (writen == -1) {
            qWarning("%s: something wrong can not write to socket %s",
                     Q_FUNC_INFO, qPrintable(curClient_->errorString()));
            return;
        } else {
            qDebug("%s: wrote %lld bytes", Q_FUNC_INFO, writen);
        }
        sendBuf_.remove(0, writen);
        curClient_->flush();
    }
}

void CommunicationMonkeyPart::clientDisconnected()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    curClient_ = nullptr;
    recvBuf_.clear();
    sendBuf_.clear();
}

void CommunicationMonkeyPart::connectionError(QAbstractSocket::SocketError err)
{
    qWarning("%s: err %d\n", Q_FUNC_INFO, static_cast<int>(err));
    if (err == QAbstractSocket::RemoteHostClosedError)
        return;
    emit error((curClient_ != nullptr)
                   ? curClient_->errorString()
                   : T_("socket err: %1").arg(static_cast<int>(err)));
}

void CommunicationMonkeyPart::sendCommand(PacketTypeForAgent pt,
                                          const QString &data)
{
    sendBuf_.append(createPacket(static_cast<uint32_t>(pt), data));
    flushSendData();
}

bool CommunicationAgentPart::connectToMonkey()
{
    assert(sock_.state() == QAbstractSocket::UnconnectedState
           && !timer_.isActive());
    assert(sock_.thread() == thread());
    assert(thread() == QThread::currentThread());
    if (sock_.state() != QAbstractSocket::UnconnectedState) {
        qWarning("%s: you try connect socket in not inital state\n",
                 Q_FUNC_INFO);
    }

    connect(&sock_, SIGNAL(bytesWritten(qint64)), this, SLOT(sendData()));
    connect(&sock_, SIGNAL(connected()), this, SLOT(sendData()));
    connect(&sock_, SIGNAL(readyRead()), this, SLOT(readCommands()));
    connect(&sock_, SIGNAL(error(QAbstractSocket::SocketError)), this,
            SLOT(connectionError(QAbstractSocket::SocketError)));

    QByteArray portnoStr = qgetenv(QTMONKEY_PORT_ENV_NAME);
    quint16 portno;
    QDataStream stream(&portnoStr, QIODevice::ReadOnly);
    stream >> portno;
    if (stream.status() == QDataStream::Ok) {
        qDebug("%s: portno %d", Q_FUNC_INFO, static_cast<int>(portno));
        sock_.connectToHost(QHostAddress::LocalHost, portno);
        timer_.start(200, this);
        return true;
    } else {
        qWarning("%s: QTMONKEY_PORT %s", Q_FUNC_INFO,
                 (portnoStr.length() == 0) ? "not defined"
                                           : "not contain suitable number");
        return false;
    }
}

void CommunicationAgentPart::readCommands()
{
    assert(sock_.state() == QAbstractSocket::ConnectedState);
    const qint64 nBytes = sock_.bytesAvailable();
    if (nBytes <= 0) {
        qWarning("%s: no data availabile: %lld\n", Q_FUNC_INFO,
                 static_cast<long long>(nBytes));
        return;
    }
    const auto wasSize = recvBuf_.size();
    recvBuf_.resize(wasSize + nBytes);
    const qint64 readBytes = sock_.read(recvBuf_.data() + wasSize, nBytes);
    if (readBytes <= 0) {
        qWarning("%s: read data error", Q_FUNC_INFO);
        recvBuf_.resize(wasSize);
        emit error(T_("Can not read data from client"));
        return;
    }
    if (nBytes != readBytes)
        recvBuf_.resize(wasSize + readBytes);
    switch (calcPacketState(recvBuf_)) {
    case PacketState::Damaged:
        qWarning("%s: packet damaged", Q_FUNC_INFO);
        emit error(T_("packet for qmonkey's agent damaged"));
        break;
    case PacketState::NotReady:
        /*nothing*/ break;
    case PacketState::Ready: {
        auto packet = extractFromPacket(recvBuf_);
        switch (static_cast<PacketTypeForAgent>(packet.first)) {
        case PacketTypeForAgent::RunScript:
            qDebug("%s: get script: '%s'", Q_FUNC_INFO,
                   qPrintable(packet.second));
            emit runScript(Script{std::move(packet.second)});
            break;
        default:
            qWarning("%s: unknown type of packet for qtmonkey's agent: %u",
                     Q_FUNC_INFO, static_cast<unsigned>(packet.first));
            emit error(T_("unknown type of packet for qtmonkey's agent"));
            break;
        }
    }
    }
}

void CommunicationAgentPart::connectionError(QAbstractSocket::SocketError err)
{
    qWarning("%s: err %d\n", Q_FUNC_INFO, static_cast<int>(err));
    emit error(sock_.errorString());
}

void CommunicationAgentPart::timerEvent(QTimerEvent *event)
{
    assert(event->timerId() == timer_.timerId());
    if (event->timerId() != timer_.timerId())
        return;
    sendData();
}

void CommunicationAgentPart::sendData()
{
    if (sock_.state() != QAbstractSocket::ConnectedState || sendBuf_.isEmpty())
        return;
    qint64 nBytes = sock_.write(sendBuf_);
    qDebug("%s: we send %lld bytes\n", Q_FUNC_INFO,
           static_cast<long long>(nBytes));
    if (nBytes == -1) {
        qWarning("%s: write to socket failed %s", Q_FUNC_INFO,
                 qPrintable(sock_.errorString()));
        return;
    }
    sendBuf_.remove(0, nBytes);
}

void CommunicationAgentPart::sendCommand(PacketTypeForMonkey pt,
                                         const QString &text)
{
    qDebug("%s: begin", Q_FUNC_INFO);
    sendBuf_.append(createPacket(static_cast<uint32_t>(pt), text));
}

void CommunicationAgentPart::flushSendData()
{
    sendData();
    if (sock_.state() == QAbstractSocket::ConnectedState)
        sock_.flush();
}
