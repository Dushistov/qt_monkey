#pragma once

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

extern QString processErrorToString(QProcess::ProcessError err);
