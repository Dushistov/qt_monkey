#pragma once

#include <ostream>

#include <QtCore/QProcess>
#include <QtCore/QString>

// for translation
#define T_(str) QString(str)

#if QT_VERSION < 0x050000
#define QStringLiteral(const_str) QString(const_str)
#endif

#if QT_VERSION >= 0x050000
#define INSTALL_QT_MSG_HANDLER(msgHandler) qInstallMessageHandler((msgHandler))
#else
#define INSTALL_QT_MSG_HANDLER(msgHandler) qInstallMsgHandler((msgHandler))
#endif

namespace qt_monkey_common
{
QString processErrorToString(QProcess::ProcessError err);
void processEventsFor(int timeoutMs);

// not implement for QString because of we may need different
// QString->QByteArray
// conversation, for example in some cases toLocal8Bit, in some cases toUtf8
inline std::ostream &operator<<(std::ostream &os, const QByteArray &str)
{
    std::copy(str.begin(), str.end(), std::ostreambuf_iterator<char>(os));
    return os;
}

extern QString searchMaxTextInWidget(QWidget &wdg);
} // namespace qt_monkey_common
