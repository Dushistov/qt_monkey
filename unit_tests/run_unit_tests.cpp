#include <QtCore/QEventLoop>
#include <QtCore/QThread>
#include <QApplication>
#include <QtTest/QSignalSpy>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>

#include "agent_qtmonkey_communication.hpp"

#if QT_VERSION >= 0x050000
#  define INSTALL_QT_MSG_HANDLER(msgHandler) qInstallMessageHandler((msgHandler))
#else
#  define INSTALL_QT_MSG_HANDLER(msgHandler) qInstallMsgHandler((msgHandler))
#endif

namespace
{
template <class Func, class Rep, class Period>
static void
processEventsForSomeTime(Func processEvents,
                         const std::chrono::duration<Rep, Period> &someTime)
{
    auto start_t = std::chrono::steady_clock::now();
    while ((std::chrono::steady_clock::now() - start_t) < someTime) {
        processEvents(
            std::chrono::duration_cast<std::chrono::milliseconds>(someTime)
                .count());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
}

TEST(QtMonkey, CommunicationBasic)
{
    using namespace qt_monkey::Private;
    using namespace std::placeholders;

    CommunicationMonkeyPart server;
    QSignalSpy serverSpy(&server, SIGNAL(newUserAppEvent(QString)));
    ASSERT_TRUE(serverSpy.isValid());
    QSignalSpy serverErr(&server, SIGNAL(error(const QString &)));
    ASSERT_TRUE(serverErr.isValid());

    class ClientThread final : public QThread
    {
    public:
        void run() override
        {
            CommunicationAgentPart client;
            QSignalSpy clientErr(&client, SIGNAL(error(const QString &)));
            ASSERT_TRUE(clientErr.isValid());
            ASSERT_TRUE(client.connectToMonkey());
            QEventLoop loop;
            auto procFunc = [&loop](int milliseconds) {
                loop.processEvents(QEventLoop::AllEvents, milliseconds);
            };
            processEventsForSomeTime(procFunc, std::chrono::milliseconds(200));
            client.sendCommand(PacketTypeForMonkey::NewUserAppEvent,
                               "Test.log(\"hi\");");
            processEventsForSomeTime(procFunc, std::chrono::milliseconds(200));
            ASSERT_EQ(0, clientErr.count());
        }
    } clientThread;
    clientThread.start();

    processEventsForSomeTime(
        [](int milliseconds) {
            qApp->processEvents(QEventLoop::AllEvents, milliseconds);
        },
        std::chrono::milliseconds(1000));
    ASSERT_EQ(0, serverErr.count());
    ASSERT_EQ(1, serverSpy.count());
    QList<QVariant> userAppEventArgs
        = serverSpy.takeFirst(); // take the first signal
    EXPECT_EQ(QString("Test.log(\"hi\");"), userAppEventArgs.at(0).toString());
    clientThread.wait(3000 /*milliseconds*/);
}

#if QT_VERSION >= 0x050000
static void msgHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
#else
static void msgHandler(QtMsgType type, const char *msg)
#endif
{
    QTextStream clog(stdout);
    switch (type) {
    case QtDebugMsg:
        clog << "Debug: " << msg << "\n";
        break;
    case QtWarningMsg:
        clog << "Warning: " << msg << "\n";
        break;
    case QtCriticalMsg:
        clog << "Critical: " << msg << "\n";
        break;
#if QT_VERSION >= 0x050000
    case QtInfoMsg:
        clog << "Info: " << msg << "\n";
        break;
#endif
    case QtFatalMsg:
        clog << "Fatal: " << msg << "\n";
        std::abort();
    }
    clog.flush();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    INSTALL_QT_MSG_HANDLER(msgHandler);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
