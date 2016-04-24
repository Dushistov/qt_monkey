#include "user_events_analyzer.hpp"

#include <QtCore/QEvent>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QMenu>
#include <QtGui/QMouseEvent>
#include <QtGui/QWidget>
#include <cstring>

using namespace qt_monkey;

namespace
{
static QString numAmongOthersWithTheSameClass(const QObject &w)
{
    QObject *p = w.parent();
    if (p == nullptr)
        return QString();

    const QObjectList &childs = p->children();
    int order = 0;
    for (QObject *obj : childs) {
        if (obj == &w) {
            if (order == 0)
                return QString();
            else
                return QString(",%1").arg(order);
            continue;
        }
        if (std::strcmp(obj->metaObject()->className(),
                        w.metaObject()->className())
            == 0)
            ++order;
    }
    return QString();
}

static QString qtObjectId(const QObject &w)
{
    const QString name = w.objectName();
    if (name.isEmpty()) {
        return QString("<class_name=%1%2>")
            .arg(w.metaObject()->className())
            .arg(numAmongOthersWithTheSameClass(w));
    }
    return name;
}

static QString mouseEventToJavaScript(QWidget &widget,
                                      const QString &widgetName,
                                      QMouseEvent *mouseEvent,
                                      const QPoint &pos)
{
    const QString mouseBtn = mouseButtonEnumToString(mouseEvent->button());

    if (mouseEvent->type() == QEvent::MouseButtonDblClick)
        return QString("Test.mouseDClick('%1', '%2', %3, %4);")
            .arg(widgetName)
            .arg(mouseBtn)
            .arg(pos.x())
            .arg(pos.y());
    else
        return QString("Test.mouseClick('%1', '%2', %3, %4);")
            .arg(widgetName)
            .arg(mouseBtn)
            .arg(pos.x())
            .arg(pos.y());
}

static bool isOnlyOneChildWithSuchClass(QObject &w)
{
    if (w.parent() == nullptr)
        return false;

    const QObjectList &childs = w.parent()->children();

    for (QObject *obj : childs)
        if (obj != &w
            && std::strcmp(obj->metaObject()->className(),
                           w.metaObject()->className())
                   == 0)
            return false;
    return true;
}

static QString qmenuActivateClick(QObject *obj, QEvent *event,
                                  const std::pair<QWidget *, QString> &widget)
{
    QString res;
    if (widget.first == nullptr || event == nullptr
        || !(event->type() == QEvent::MouseButtonDblClick
             || event->type() == QEvent::MouseButtonPress)
        || std::strcmp(widget.first->metaObject()->className(), "QMenu") != 0)
        return res;
    auto qm = qobject_cast<QMenu *>(widget.first);
    QAction *act = qm->actionAt(widget.first->mapFromGlobal(
        static_cast<QMouseEvent *>(event)->globalPos()));
    if (act != nullptr) {
        qDebug("%s: act->text() %s", Q_FUNC_INFO, qPrintable(act->text()));
        if (!widget.first->objectName()
                 .isEmpty() /*widgetName != "<unknown name>"*/)
            res = QString("Test.activateItem('%1', '%2');")
                      .arg(widget.second)
                      .arg(act->text());
        else
            res = QString("Test.activateMenuItem('%1');").arg(act->text());
    }
    return res;
}
}

QString qt_monkey::mouseButtonEnumToString(Qt::MouseButton b)
{
    static const std::pair<Qt::MouseButton, QLatin1String> mouseBtnNames[] = {
        {Qt::LeftButton, QLatin1String("Qt.LeftButton")},
        {Qt::RightButton, QLatin1String("Qt.RightButton")},
        {Qt::MidButton, QLatin1String("Qt.MidButton")},
    };

    for (auto &&elm : mouseBtnNames)
        if (elm.first == b)
            return elm.second;

    return QLatin1String("<unknown button>");
}

QString qt_monkey::fullQtWidgetId(const QWidget &w)
{
    QString res = qtObjectId(w);
    qDebug("%s: class name %s, id %s", Q_FUNC_INFO, w.metaObject()->className(),
           qPrintable(res));
    QObject *cur_obj = w.parent();
    while (cur_obj != nullptr) {
        res = qtObjectId(*cur_obj) + "." + res;
        cur_obj = cur_obj->parent();
    }
    return res;
}

UserEventsAnalyzer::UserEventsAnalyzer(
    std::list<CustomEventAnalyzer> customEventAnalyzers, QObject *parent)
    : QObject(parent), customEventAnalyzers_(std::move(customEventAnalyzers))
{
    customEventAnalyzers_.emplace_back(qmenuActivateClick);
}

QString UserEventsAnalyzer::callCustomEventAnalyzers(
    QObject *obj, QEvent *event,
    const std::pair<QWidget *, QString> &widget) const
{
    QString code;
    for (auto &&userCustomAnalyzer : customEventAnalyzers_) {
        code = userCustomAnalyzer(obj, event, widget);
        if (!code.isEmpty())
            return code;
    }
    return code;
}

bool UserEventsAnalyzer::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        qDebug("%s: key event for '%s'\n", Q_FUNC_INFO,
               qPrintable(obj->objectName()));
        break;
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonPress: {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        qDebug("%s: mouse event for '%s': %s\n", Q_FUNC_INFO,
               qPrintable(obj->objectName()),
               event->type() == QEvent::MouseButtonDblClick ? "double click"
                                                            : "release event");
        QWidget *w = QApplication::widgetAt(mouseEvent->globalPos());
        if (w == nullptr) {
            QPoint p = mouseEvent->globalPos();
            qDebug(
                "(%s, %d): Can not find out what widget is used(x %d, y %d)!!!",
                Q_FUNC_INFO, __LINE__, p.x(), p.y());
            return false;
        }

        QPoint pos = w->mapFromGlobal(mouseEvent->globalPos());
        QString widgetName = fullQtWidgetId(*w);
        QString scriptLine
            = callCustomEventAnalyzers(obj, event, {w, widgetName});
        if (scriptLine.isEmpty())
            scriptLine = mouseEventToJavaScript(*w, widgetName, mouseEvent, pos);

        if (w->objectName().isEmpty() && !isOnlyOneChildWithSuchClass(*w)) {
            QWidget *baseWidget = w;
            while (w != nullptr && w->objectName().isEmpty())
                w = qobject_cast<QWidget *>(w->parent());
            if (w != nullptr && w != baseWidget) {
                pos = w->mapFromGlobal(mouseEvent->globalPos());
                widgetName = fullQtWidgetId(*w);
                QString anotherScript = callCustomEventAnalyzers(obj, event, {w, widgetName});
                if (anotherScript.isEmpty())
                    anotherScript = mouseEventToJavaScript(*w, widgetName, mouseEvent, pos);
                if (scriptLine != anotherScript)
                    scriptLine = QString("%1\n//%2")
                                     .arg(scriptLine)
                                     .arg(anotherScript);
            }
        }

        emit userEventInScriptForm(scriptLine);
        break;
    } // event by mouse
    default: {
        const QString code = callCustomEventAnalyzers(obj, event, {nullptr, QString()});
        if (!code.isEmpty())
            emit userEventInScriptForm(code);
        break;
    }
    } // switch (event->type())
    return QObject::eventFilter(obj, event);
}
