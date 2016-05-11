#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QTextStream>

#include "common.hpp"
#include "qtmonkey.hpp"

using qt_monkey_common::operator<<;

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
    static auto startTime = std::chrono::steady_clock::now();
    using std::clog;
    const double timeDiff
        = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - startTime)
              .count()
          / 1000.;
    switch (type) {
    case QtDebugMsg:
        clog << "Debug(" << timeDiff << "): " << msg << std::endl;
        break;
    case QtWarningMsg:
        clog << "Warning(" << timeDiff << "): " << msg << std::endl;
        break;
    case QtCriticalMsg:
        clog << "Critical(" << timeDiff << "): " << msg << std::endl;
        break;
#if QT_VERSION >= 0x050000
    case QtInfoMsg:
        clog << "Info(" << timeDiff << "): " << msg << std::endl;
        break;
#endif
    case QtFatalMsg:
        clog << "Fatal(" << timeDiff << "): " << msg << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

static QString usage()
{
    return T_("Usage: %1 [--exit-on-script-error] [--encoding file_encoding] "
              "[--script path/to/script] "
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
    const char *encoding = "UTF-8";
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
        } else if (std::strcmp(argv[i], "--encoding") == 0) {
            if ((i + 1) >= argc) {
                std::cerr << qPrintable(usage());
                return EXIT_FAILURE;
            }
            ++i;
            encoding = argv[i];
        } else if (std::strcmp(argv[i], "--help") == 0
                   || std::strcmp(argv[i], "-h") == 0) {
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

    if (!scripts.empty()
        && !monkey.runScriptFromFile(std::move(scripts), encoding))
        return EXIT_FAILURE;

    monkey.runApp(QString::fromLocal8Bit(argv[userAppOffset]),
                  std::move(userAppArgs));

    return app.exec();
}
