#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QTextStream>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "common.hpp"
#include "qtmonkey.hpp"

using qt_monkey_common::operator <<;

namespace
{
class ConsoleApplication final : public QCoreApplication
{
public:
    ConsoleApplication(int &argc, char **argv) : QCoreApplication(argc, argv) {}
    bool notify(QObject *receiver, QEvent *event) override
    {
        try {
            return QCoreApplication::notify(receiver, event);
        } catch (const std::exception &ex) {
            qFatal("%s: catch exception: %s", Q_FUNC_INFO, ex.what());
            return false;
        }
    }
};

static inline std::ostream &operator<<(std::ostream &os, const QString &str)
{
    os << str.toLocal8Bit();
    return os;
}
}

#if QT_VERSION >= 0x050000
static void msgHandler(QtMsgType type, const QMessageLogContext &,
                       const QString &msg)
#else
static void msgHandler(QtMsgType type, const char *msg)
#endif
{
    using std::clog;
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
        clog.flush();
        std::exit(EXIT_FAILURE);
    }
    clog.flush();
}

static QString usage()
{
    return T_("Usage: %1 [--exit-on-script-error] [--script path/to/script] "
              "--user-app "
              "path/to/application [application's command line args]\n")
        .arg(QCoreApplication::applicationFilePath());
}

int main(int argc, char *argv[])
{
    std::ios_base::sync_with_stdio(false);
    ConsoleApplication app(argc, argv);

    INSTALL_QT_MSG_HANDLER(msgHandler);
    bool exitOnScriptError = false;
    int userAppOffset = -1;
    QStringList scripts;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--user-app") == 0) {
            if ((i + 1) >= argc) {
                std::cerr << qPrintable(usage());
                return EXIT_FAILURE;
            }
            ++i;
            userAppOffset = i;
            break;
        } else if (std::strcmp(argv[i], "--script") == 0) {
            if ((i + 1) >= argc) {
                std::cerr << qPrintable(usage());
                return EXIT_FAILURE;
            }
            ++i;
            scripts.append(QFile::decodeName(argv[i]));
        } else if (std::strcmp(argv[i], "--exit-on-script-error") == 0) {
            exitOnScriptError = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << qPrintable(usage());
            return EXIT_SUCCESS;
        } else {
            std::cerr << qPrintable(T_("Unknown option: %1\n").arg(argv[i]))
                      << qPrintable(usage());
            return EXIT_FAILURE;
        }
    if (userAppOffset == -1) {
        std::cerr << qPrintable(
            T_("You should set path and args for user app with --user-app\n"));
        return EXIT_FAILURE;
    }
    QStringList userAppArgs;
    for (int i = userAppOffset + 1; i < argc; ++i)
        userAppArgs << QString::fromLocal8Bit(argv[i]);
    qt_monkey_app::QtMonkey monkey(exitOnScriptError);

    if (!scripts.empty() && !monkey.runScriptFromFile(std::move(scripts)))
        return EXIT_FAILURE;

    monkey.runApp(QString::fromLocal8Bit(argv[userAppOffset]),
                  std::move(userAppArgs));

    return app.exec();
}
