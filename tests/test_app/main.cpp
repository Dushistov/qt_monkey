#include <QApplication>
#include <QDesktopWidget>
#include <QtScript/QScriptEngine>

#include "agent.hpp"
#include "mainwin.hpp"
#include "my_custom_button.hpp"
#include "script_ext.hpp"

static QString
myCustomButtonAnalyzer(const qt_monkey_agent::EventInfo &eventInfo)
{
    QString res;
    if (eventInfo.widget == nullptr
        || eventInfo.event->type() != QEvent::MouseButtonPress)
        return res;
    auto btn = qobject_cast<MyCustomButton *>(eventInfo.widget);
    if (btn == nullptr)
        return res;
    qDebug("%s: it is our custom button", Q_FUNC_INFO);
    return QString("ExtAPI.pressButton(\"%1\");").arg(btn->text());
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ScriptExt scriptExt;
    qt_monkey_agent::Agent agent(
        QKeySequence(Qt::Key_F12 | Qt::SHIFT), {myCustomButtonAnalyzer},
        [&scriptExt](QScriptEngine &engine) {
            QScriptValue global = engine.globalObject();
            QScriptValue ext_api_js_obj = engine.newQObject(&scriptExt);
            QScriptValue metaObject
                = engine.newQMetaObject(&ScriptExt::staticMetaObject);
            global.setProperty("ExtAPIClass", metaObject);
            global.setProperty(QLatin1String("ExtAPI"), ext_api_js_obj);
        });
    MainWin mainwin;
    const QDesktopWidget *desc = QApplication::desktop();
    mainwin.resize(desc->width() / 4, desc->height() / 4);
    mainwin.show();
    return app.exec();
}
