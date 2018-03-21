#include "qtmonkey_gui.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtCore/QTextCodec>
#include <QtCore/QTextStream>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "common.hpp"
#include "json11.hpp"
#include "qtmonkey_app_api.hpp"

#define SETUP_WIN_CTRL(__name__)                                               \
    auto __name__ = getMonkeyCtrl();                                           \
    if (__name__ == nullptr)                                                   \
        return;

static const QLatin1String prefsDomain{"qt_monkey"};
static const QLatin1String prefsSectName{"main"};
static const QLatin1String testAppPathPrefName{"path to test app"};
static const QLatin1String testAppArgsPrefName{"test app arguments"};
static const QLatin1String protocolModePrefName{"protocol mode"};
static const QLatin1String pathToScriptDirPrefName{"path to script dir"};

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
    emit monkeyAppFinishedSignal(qt_monkey_common::processErrorToString(err));
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
    jsonFromMonkey_.append(out);
    qDebug("%s: json |%s|", Q_FUNC_INFO, qPrintable(jsonFromMonkey_));

    std::string::size_type parserStopPos;
    qt_monkey_app::parseOutputFromMonkeyApp(
        {jsonFromMonkey_.constData(),
         static_cast<size_t>(jsonFromMonkey_.size())},
        parserStopPos,
        [this](QString eventScriptLines) {
            emit monkeyAppNewEvent(std::move(eventScriptLines));
        },
        [this](QString userAppErrors) {
            emit monkeyUserAppError(std::move(userAppErrors));
        },
        [this]() { // on script end
            emit monkeyScriptEnd();
        },
        [this](QString scriptLog) { emit monkeScriptLog(scriptLog); },
        [this](QString data) {
            qtmonkeyApp_.kill();
            emit monkeyAppFinishedSignal(
                T_("Internal Error: problem with monkey<->gui protocol: %1")
                    .arg(data));
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

void QtMonkeyAppCtrl::runScript(const QString &script,
                                const QString &scriptFileName)
{
    const std::string data
        = qt_monkey_app::createPacketFromRunScript(script, scriptFileName)
          + '\n';
    quint64 sentBytes = 0;
    do {
        qint64 nbytes = qtmonkeyApp_.write(data.data() + sentBytes,
                                           data.size() - sentBytes);
        if (nbytes < 0) {
            emit criticalError(T_("Can not send data to application"));
            return;
        }
        sentBytes += nbytes;
    } while (sentBytes < data.size());
}

QtMonkeyWindow::QtMonkeyWindow(QWidget *parent) : QWidget(parent)
{
    setupUi(this);
    loadPrefs();
    connect(&savePrefsTimer_, SIGNAL(timeout()), this, SLOT(savePrefs()));
    pbSaveScript_->setEnabled(false);
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
        connect(monkeyCtrl_, SIGNAL(monkeyScriptEnd()), this,
                SLOT(onMonkeyScriptEnd()));
        connect(monkeyCtrl_, SIGNAL(monkeScriptLog(const QString &)), this,
                SLOT(onMonkeScriptLog(const QString &)));
        connect(monkeyCtrl_, SIGNAL(criticalError(const QString &)), this,
                SLOT(showError(const QString &)));
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
    else
        logNewLine(MsgType::Default, T_("The application has exited"));
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
    cfg.setValue(protocolModePrefName, cbProtocolRunning_->isChecked());
    cfg.setValue(pathToScriptDirPrefName, scriptDir_);
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
    const bool protocolMode = cfg.value(protocolModePrefName, false).toBool();
    cbProtocolRunning_->setChecked(protocolMode);
    scriptDir_
        = cfg.value(pathToScriptDirPrefName, QDir::homePath()).toString();
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
    qDebug("%s: scriptLine %s, state %d", Q_FUNC_INFO, qPrintable(scriptLine),
           static_cast<int>(state_));
    if (state_ == State::RecordEvents) {
        if (cbInsertEventsAtCursor_->checkState() == Qt::Checked)
            teScriptEdit_->insertPlainText(scriptLine + "\n");
        else
            teScriptEdit_->appendPlainText(scriptLine);
    } else if (state_ == State::PlayingEvents
               && cbProtocolRunning_->isChecked()) {
        logNewLine(MsgType::Protocol, scriptLine);
    }
}

void QtMonkeyWindow::onMonkeyUserAppError(const QString &errMsg)
{
    logNewLine(MsgType::Error, errMsg);
}

void QtMonkeyWindow::logNewLine(MsgType msgType, const QString &msg)
{
    QString color;
    switch (msgType) {
    case MsgType::Default:
        break;
    case MsgType::Error:
        color = QStringLiteral("DeepPink");
        break;
    case MsgType::Protocol:
        color = QStringLiteral("Lime");
        break;
    }
    QString text;
    if (!color.isEmpty())
        text = QStringLiteral("<font color=\"%1\">").arg(color);
#if QT_VERSION < 0x050000
    text += Qt::escape(msg);
#else
    text += msg.toHtmlEscaped();
#endif
    text.replace("\n", "<br>");
    if (!color.isEmpty())
        text += "</font>";

    text += "<br>";

    teLog_->insertHtml(text);
}

void QtMonkeyWindow::onMonkeyScriptEnd() { changeState(State::DoNothing); }

void QtMonkeyWindow::onMonkeScriptLog(const QString &msg)
{
    logNewLine(MsgType::Default, msg);
}

void QtMonkeyWindow::on_pbRunScript__pressed()
{
    SETUP_WIN_CTRL(ctrl)
    const QString demoModeStr
        = QStringLiteral("Test.setDemonstrationMode(%1);")
              .arg(cbDemonstrationMode_->isChecked() ? "true" : "false");
    ctrl->runScript(demoModeStr + teScriptEdit_->toPlainText());
    changeState(State::PlayingEvents);
}

void QtMonkeyWindow::changeState(State val)
{
    qDebug("%s: begin was val %d, now val %d", Q_FUNC_INFO,
           static_cast<int>(state_), static_cast<int>(val));
    state_ = val;
    switch (state_) {
    case State::DoNothing:
        teScriptEdit_->setReadOnly(false);
        pbRunScript_->setEnabled(true);
        pbStartRecording_->setEnabled(true);
        break;
    case State::RecordEvents:
        teScriptEdit_->setReadOnly(false);
        pbRunScript_->setEnabled(true);
        pbStartRecording_->setEnabled(false);
        break;
    case State::PlayingEvents:
        teScriptEdit_->setReadOnly(true);
        pbRunScript_->setEnabled(false);
        pbStartRecording_->setEnabled(false);
        break;
    }
}

void QtMonkeyWindow::on_pbClearLog__pressed() { teLog_->clear(); }

void QtMonkeyWindow::on_cbProtocolRunning__toggled(bool checked)
{
    qDebug("%s: begin", Q_FUNC_INFO);
    assert(checked == cbProtocolRunning_->isChecked());
    scheduleSave();
}

QtMonkeyWindow::~QtMonkeyWindow() { savePrefs(); }

void QtMonkeyWindow::on_pbSaveScriptToFile__pressed()
{
    const QString fn
        = QFileDialog::getSaveFileName(this, T_("Save script to"), scriptDir_,
                                       T_("Qt java script (*.qs *.js)"));
    if (fn.isEmpty())
        return;
    const QFileInfo fi(fn);

    scriptDir_ = fi.dir().absolutePath();
    scheduleSave();

    saveScriptToFile(fn);
    scriptFileName_ = fn;
}

void QtMonkeyWindow::saveScriptToFile(const QString &fileName)
{
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, T_("Error"),
                              T_("Can not open for writing: %1").arg(fileName));
        return;
    }

    QTextStream t(&f);
    t.setCodec(QTextCodec::codecForName(encoding_));
    t << teScriptEdit_->toPlainText();
}

void QtMonkeyWindow::on_pbLoadScriptFromFile__pressed()
{
    const QString fn
        = QFileDialog::getOpenFileName(this, T_("Load script from"), scriptDir_,
                                       T_("Qt java script (*.qs *.js)"));
    if (fn.isEmpty())
        return;
    QFileInfo fi(fn);
    scriptDir_ = fi.dir().absolutePath();
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, T_("Error"),
                              T_("Can not open for reading: %1").arg(fn));
        return;
    }

    QString text;
    QTextStream t(&f);
    t.setCodec(QTextCodec::codecForName(encoding_));
    text = t.readAll();
    f.close();
    teScriptEdit_->setPlainText(text);
    scriptFileName_ = fn;
    pbSaveScript_->setEnabled(!scriptFileName_.isEmpty());
}

void QtMonkeyWindow::on_pbSaveScript__pressed()
{
    saveScriptToFile(scriptFileName_);
}

int main(int argc, char *argv[])
{
    const char *enc = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--encoding") == 0) {
            if ((i + 1) >= argc) {
                qFatal("Usage: %s [--encoding scripts_charset]", argv[0]);
                return EXIT_FAILURE;
            }
            ++i;
            enc = argv[i];
        }
    }
    QApplication app(argc, argv);

    QtMonkeyWindow mw;
    if (enc != nullptr) {
        mw.setEncoding(enc);
    }
    mw.show();
    return app.exec();
}
