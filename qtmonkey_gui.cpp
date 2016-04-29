#include "qtmonkey_gui.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <cassert>

#include "common.hpp"
#include "json11.hpp"
#include "qtmonkey_app_api.hpp"

using json11::Json;

#define SETUP_WIN_CTRL(__name__)                                               \
    auto __name__ = getMonkeyCtrl();                                           \
    if (__name__ == nullptr)                                                   \
        return;

QtMonkeyAppCtrl::QtMonkeyAppCtrl(const QString &appPath,
                                 const QStringList &appArgs, QObject *parent)
    : QObject(parent)
{
    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QString monkeyAppFileName = QFile::decodeName(QTMONKEY_APP_NAME);
    const QString monkeyAppPath
        = appDirPath + QDir::separator() + monkeyAppFileName;
    const QFileInfo monkeyAppFileInfo{monkeyAppPath};
    if (!(monkeyAppFileInfo.exists() && monkeyAppFileInfo.isFile()
          && monkeyAppFileInfo.isExecutable()))
        throw std::runtime_error(
            T_("Can not find %1").arg(monkeyAppPath).toStdString());

    connect(&qtmonkeyApp_, SIGNAL(error(QProcess::ProcessError)), this,
            SLOT(monkeyAppError(QProcess::ProcessError)));
    connect(&qtmonkeyApp_, SIGNAL(finished(int, QProcess::ExitStatus)), this,
            SLOT(monkeyAppFinished(int, QProcess::ExitStatus)));
    connect(&qtmonkeyApp_, SIGNAL(readyReadStandardOutput()), this,
            SLOT(monkeyAppNewOutput()));
    connect(&qtmonkeyApp_, SIGNAL(readyReadStandardError()), this,
            SLOT(monkeyAppNewErrOutput()));

    QStringList args;
    args << QStringLiteral("--user-app") << appPath << appArgs;
    qtmonkeyApp_.start(monkeyAppPath, args);
}

void QtMonkeyAppCtrl::monkeyAppError(QProcess::ProcessError err)
{
    qDebug("%s: err %d", Q_FUNC_INFO, static_cast<int>(err));
    emit monkeyAppFinishedSignal(processErrorToString(err));
}

void QtMonkeyAppCtrl::monkeyAppFinished(int exitCode,
                                        QProcess::ExitStatus exitStatus)
{
    qDebug("%s: begin exitCode %d, exitStatus %d", Q_FUNC_INFO, exitCode,
           static_cast<int>(exitStatus));
    if (exitCode != EXIT_SUCCESS)
        emit monkeyAppFinishedSignal(T_("monkey app exit status not %1: %2")
                                         .arg(EXIT_SUCCESS)
                                         .arg(exitCode));
    else
        emit monkeyAppFinishedSignal(QString());
}

void QtMonkeyAppCtrl::monkeyAppNewOutput()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    const QByteArray out = qtmonkeyApp_.readAllStandardOutput();
    jsonFromMonkey_.append(out.constData(), out.size());
    qDebug("%s: json |%s|", Q_FUNC_INFO, qPrintable(jsonFromMonkey_));

    std::string::size_type parserStopPos;
    qt_monkey_app::parseOutputFromMonkeyApp(jsonFromMonkey_, parserStopPos,
                                            [this](QString eventScriptLines) {
                                                emit monkeyAppNewEvent(std::move(eventScriptLines));
                                            },
                                            [this](QString userAppErrors) {
                                                emit monkeyUserAppError(std::move(userAppErrors));
                                            },
                                            [this]() {//on script end
                                            },
                                            [this](QString scriptLog) {
                                            },
                                            [this](QString data) {
                                                qtmonkeyApp_.kill();
                                                emit monkeyAppFinishedSignal(
                                                    T_("Internal Error: problem with monkey<->gui protocol: %1").arg(data));
                                            });

    if (parserStopPos != 0)
        jsonFromMonkey_.remove(0, parserStopPos);
}

void QtMonkeyAppCtrl::monkeyAppNewErrOutput()
{
    const QString errOut
        = QString::fromLocal8Bit(qtmonkeyApp_.readAllStandardError());
    qDebug("MONKEY: %s", qPrintable(errOut));
}

QtMonkeyWindow::QtMonkeyWindow(QWidget *parent) : QWidget(parent)
{
    setupUi(this);
    loadPrefs();
    connect(&savePrefsTimer_, SIGNAL(timeout()), this, SLOT(savePrefs()));
}

void QtMonkeyWindow::showError(const QString &msg)
{
    QMessageBox::critical(this, T_("Error"), msg);
}

static QStringList splitCommandLine(const QString &cmdLine)
{
    QStringList list;
    QString arg;
    bool escape = false;
    enum { Idle, Arg, QuotedArg } state = Idle;
    foreach (QChar const c, cmdLine) {
        if (!escape && c == '\\') {
            escape = true;
            continue;
        }
        switch (state) {
        case Idle:
            if (!escape && c == '"')
                state = QuotedArg;
            else if (escape || !c.isSpace()) {
                arg += c;
                state = Arg;
            }
            break;
        case Arg:
            if (!escape && c == '"') {
                state = QuotedArg;
            } else if (escape || !c.isSpace()) {
                arg += c;
            } else {
                list << arg;
                arg.clear();
                state = Idle;
            }
            break;
        case QuotedArg:
            if (!escape && c == '"')
                state = arg.isEmpty() ? Idle : Arg;
            else
                arg += c;
            break;
        }
        escape = false;
    }
    if (!arg.isEmpty())
        list << arg;
    return list;
}

QtMonkeyAppCtrl *QtMonkeyWindow::getMonkeyCtrl() try {
    if (monkeyCtrl_ == nullptr) {
        monkeyCtrl_ = new QtMonkeyAppCtrl(
            leTestApp_->text(), splitCommandLine(leTestAppArgs_->text()), this);
        connect(monkeyCtrl_, SIGNAL(monkeyAppFinishedSignal(QString)), this,
                SLOT(onMonkeyAppFinishedSignal(QString)));
        connect(monkeyCtrl_, SIGNAL(monkeyAppNewEvent(const QString &)), this,
                SLOT(onMonkeyAppNewEvent(const QString &)));
        connect(monkeyCtrl_, SIGNAL(monkeyUserAppError(const QString &)), this,
                SLOT(onMonkeyUserAppError(const QString &)));
    }
    return monkeyCtrl_;
} catch (const std::exception &ex) {
    showError(QString::fromStdString(ex.what()));
    return nullptr;
}

void QtMonkeyWindow::onMonkeyAppFinishedSignal(QString msg)
{
    qDebug("%s: msg '%s'", Q_FUNC_INFO, qPrintable(msg));
    if (!msg.isEmpty())
        showError(msg);
    if (monkeyCtrl_ != nullptr) {
        monkeyCtrl_->deleteLater();
        monkeyCtrl_ = nullptr;
    }
    changeState(State::DoNothing);
}

void QtMonkeyWindow::on_pbStartRecording__pressed()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    SETUP_WIN_CTRL(ctrl)
    changeState(State::RecordEvents);
}

void QtMonkeyWindow::on_leTestApp__textEdited(const QString &text)
{
    qDebug("%s: begin", Q_FUNC_INFO);
    assert(text == leTestApp_->text());
    scheduleSave();
}

void QtMonkeyWindow::on_leTestAppArgs__textEdited(const QString &text)
{
    assert(text == leTestAppArgs_->text());
    scheduleSave();
}

static const QLatin1String prefsDomain{"qt_monkey"};
static const QLatin1String prefsSectName{"main"};
static const QLatin1String testAppPathPrefName{"path to test app"};
static const QLatin1String testAppArgsPrefName{"test app arguments"};

void QtMonkeyWindow::savePrefs()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    QSettings cfg(prefsDomain, prefsDomain);
    if (!(cfg.status() == QSettings::NoError && cfg.isWritable())) {
        qWarning("%s: can not save prefs", Q_FUNC_INFO);
        return;
    }
    cfg.beginGroup(prefsSectName);
    cfg.setValue(testAppPathPrefName, leTestApp_->text());
    cfg.setValue(testAppArgsPrefName, leTestAppArgs_->text());
    cfg.endGroup();
    cfg.sync();
}

void QtMonkeyWindow::loadPrefs()
{
    QSettings cfg(prefsDomain, prefsDomain);
    if (!(cfg.status() == QSettings::NoError)) {
        qWarning("%s: can not save prefs", Q_FUNC_INFO);
        return;
    }
    cfg.beginGroup(prefsSectName);
    const QString testAppPath
        = cfg.value(testAppPathPrefName, QString{}).toString();
    leTestApp_->setText(testAppPath);
    const QString testAppArgs
        = cfg.value(testAppArgsPrefName, QString{}).toString();
    leTestAppArgs_->setText(testAppArgs);
}

void QtMonkeyWindow::scheduleSave()
{
    if (savePrefsTimer_.isActive())
        return;
    savePrefsTimer_.setSingleShot(true);
    savePrefsTimer_.start(2000 /*ms*/);
}

void QtMonkeyWindow::on_pbBrowse__pressed()
{
    qDebug("%s: begin", Q_FUNC_INFO);
    const QFileInfo fi{leTestApp_->text()};
    const QString fn = QFileDialog::getOpenFileName(
        this, T_("Choose application"), fi.dir().absolutePath());
    if (fn.isEmpty())
        return;
    leTestApp_->setText(fn);
    scheduleSave();
}

void QtMonkeyWindow::onMonkeyAppNewEvent(const QString &scriptLine)
{
    if (state_ != State::RecordEvents)
        return;
    if (cbInsertEventsAtCursor_->checkState() == Qt::Checked)
        teScriptEdit_->insertPlainText(scriptLine);
    else
        teScriptEdit_->append(scriptLine);
}

void QtMonkeyWindow::onMonkeyUserAppError(const QString &errMsg)
{
    logNewLine(QtCriticalMsg, errMsg);
}

void QtMonkeyWindow::logNewLine(QtMsgType, const QString &msg)
{
    teLog_->append(msg);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QtMonkeyWindow mw;
    mw.show();
    return app.exec();
}
