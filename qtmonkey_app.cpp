#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <QtCore/QCoreApplication>
#include <QtCore/QTextStream>
#include <QtCore/QProcess>

#include "common.hpp"
#include "qtmonkey.hpp"

#if QT_VERSION >= 0x050000
static void msgHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
#else
static void msgHandler(QtMsgType type, const char *msg)
#endif
{
    QTextStream clog(stderr);
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
    QCoreApplication app(argc, argv);

    INSTALL_QT_MSG_HANDLER(msgHandler);

    QTextStream cout(stdout);
    QTextStream cerr(stderr);

    int userAppOffset = -1;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--user-app") == 0) {
            if ((i + 1) >= argc) {
                cerr << T_("Usage: %s --user-app path/to/application [application's command line args]\n").arg(argv[0]);
                return EXIT_FAILURE;
            }
            userAppOffset = i + 1;
            break;
        } else {
            cerr << T_("Unkown option %s\n").arg(argv[i]);
            return EXIT_FAILURE;
        }
    if (userAppOffset == -1) {
        cerr << T_("You should set path and args for user app with --user-app\n");
        return EXIT_FAILURE;
    }
    QStringList userAppArgs;
    for (int i = userAppOffset + 1; i < argc; ++i)
        userAppArgs << QString::fromLocal8Bit(argv[i]);
    QtMonkey monkey(QString::fromLocal8Bit(argv[userAppOffset]), std::move(userAppArgs));

    return app.exec();
}
