#include <QtCore/QThread>
#include <QtGui/QApplication>
#include <QtTest/QSignalSpy>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>

#include "agent_qtmonkey_communication.hpp"

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
            processEventsForSomeTime(
                [&loop](int milliseconds) {
                    loop.processEvents(QEventLoop::AllEvents, milliseconds);
                },
                std::chrono::milliseconds(200));
            client.sendCommand(PacketTypeForMonkey::NewUserAppEvent,
                               "Test.log(\"hi\");");
            processEventsForSomeTime(
                [&loop](int milliseconds) {
                    loop.processEvents(QEventLoop::AllEvents, milliseconds);
                },
                std::chrono::milliseconds(500));
            ASSERT_EQ(0, clientErr.count());
        }
    } clientThread;
    clientThread.start();

    processEventsForSomeTime([](int milliseconds) { qApp->processEvents(QEventLoop::AllEvents, milliseconds); },
                             std::chrono::milliseconds(1000));
    ASSERT_EQ(0, serverErr.count());
    ASSERT_EQ(1, serverSpy.count());
    QList<QVariant> userAppEventArgs
        = serverSpy.takeFirst(); // take the first signal
    EXPECT_EQ(QString("Test.log(\"hi\");"), userAppEventArgs.at(0).toString());
    clientThread.wait(3000 /*milliseconds*/);
}

static void msgHandler(QtMsgType type, const char *msg)
{
    switch (type) {
    case QtDebugMsg:
        std::clog << "Debug: " << msg << "\n";
        break;
    case QtWarningMsg:
        std::clog << "Warning: " << msg << "\n";
        break;
    case QtCriticalMsg:
        std::clog << "Critical: " << msg << "\n";
        break;
    case QtFatalMsg:
        std::clog << "Fatal: " << msg << "\n";
        std::abort();
    }
    std::clog.flush();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qInstallMsgHandler(msgHandler);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
