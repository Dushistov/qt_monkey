//#define DEBUG_ANALYZER
#include "user_events_analyzer.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QShortcutEvent>
#include <QTreeWidget>
#include <QWidget>
#include <QtCore/QEvent>

#include "agent.hpp"
#include "common.hpp"

using qt_monkey_agent::UserEventsAnalyzer;
using qt_monkey_agent::GenerateCommand;
using qt_monkey_agent::EventInfo;
using qt_monkey_agent::CustomEventAnalyzer;
using qt_monkey_agent::Private::TreeWidgetWatcher;
using qt_monkey_agent::Private::TreeViewWatcher;
using qt_monkey_agent::Private::MacMenuActionWatcher;

#ifdef DEBUG_ANALYZER
#define DBGPRINT(fmt, ...) qDebug(fmt, __VA_ARGS__)
#else
#define DBGPRINT(fmt, ...)                                                     \
    do {                                                                       \
    } while (false)
#endif

namespace
{

static constexpr int repeatEventTimeoutMs = 100;

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

static QString keyReleaseEventToScript(const QString &widgetName,
                                       QKeyEvent *keyEvent)
{
    if (keyEvent->key() == 0)
        return QStringLiteral("//some special key");

    DBGPRINT("%s: widgtName: %s, key %X, %s", Q_FUNC_INFO,
             qPrintable(widgetName), keyEvent->key(),
             keyEvent->key() != Qt::Key_unknown ? "known key" : "unknown key");

    int modifiers[4] = {0, 0, 0, 0};
    int curMod = 0;

    if (keyEvent->modifiers() & Qt::ShiftModifier)
        modifiers[curMod++] = Qt::ShiftModifier;

    if (keyEvent->modifiers() & Qt::AltModifier)
        modifiers[curMod++] = Qt::AltModifier;

    if (keyEvent->modifiers() & Qt::ControlModifier)
        modifiers[curMod++] = Qt::ControlModifier;

    if (keyEvent->modifiers() & Qt::MetaModifier)
        modifiers[curMod++] = Qt::MetaModifier;

    QKeySequence keySeq;
    switch (curMod) {
    case 1:
        if (keyEvent->key() != Qt::Key_unknown)
            keySeq = QKeySequence(modifiers[0], keyEvent->key());
        else
            keySeq = QKeySequence(modifiers[0]);
        break;
    case 2:
        if (keyEvent->key() != Qt::Key_unknown)
            keySeq = QKeySequence(modifiers[0], modifiers[1], keyEvent->key());
        else
            keySeq = QKeySequence(modifiers[0], modifiers[1]);
        break;
    case 3:
        if (keyEvent->key() != Qt::Key_unknown)
            keySeq = QKeySequence(modifiers[0], modifiers[1], modifiers[2],
                                  keyEvent->key());
        else
            keySeq = QKeySequence(modifiers[0], modifiers[1], modifiers[2]);
        break;
    case 4:
        keySeq = QKeySequence(keyEvent->text());
        break;
    case 0:
    default:
        keySeq = QKeySequence(keyEvent->key());
        break;
    }

    return QStringLiteral("Test.keyClick('%1', '%2');")
        .arg(widgetName)
        .arg(keySeq.toString());
}

static QString mouseEventToJavaScript(const QString &widgetName,
                                      QMouseEvent *mouseEvent,
                                      const QPoint &pos)
{
    const QString mouseBtn
        = qt_monkey_agent::mouseButtonEnumToString(mouseEvent->button());

    if (mouseEvent->type() == QEvent::MouseButtonDblClick)
        return QStringLiteral("Test.mouseDClick('%1', '%2', %3, %4);")
            .arg(widgetName)
            .arg(mouseBtn)
            .arg(pos.x())
            .arg(pos.y());
    else
        return QStringLiteral("Test.mouseClick('%1', '%2', %3, %4);")
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

static QString qmenuActivateClick(const EventInfo &eventInfo)
{
    QString res;
    QWidget *widget = eventInfo.widget;
    QEvent *event = eventInfo.event;
    if (widget == nullptr || event == nullptr
        || !(event->type() == QEvent::MouseButtonDblClick
             || event->type() == QEvent::MouseButtonPress)
        || std::strcmp(widget->metaObject()->className(), "QMenu") != 0)
        return res;
    auto qm = qobject_cast<QMenu *>(widget);
    QAction *act = qm->actionAt(
        widget->mapFromGlobal(static_cast<QMouseEvent *>(event)->globalPos()));
    if (act != nullptr) {
        DBGPRINT("%s: act->text() %s", Q_FUNC_INFO, qPrintable(act->text()));
        if (!widget->objectName().isEmpty() /*widgetName != "<unknown name>"*/)
            res = QString("Test.activateItem('%1', '%2');")
                      .arg(eventInfo.widgetName)
                      .arg(act->text());
        else
            res = QString("Test.activateMenuItem('%1');").arg(act->text());
    }
    return res;
}

static QWidget *searchThroghSuperClassesAndParents(QWidget *widget,
                                                   const char *wname,
                                                   size_t limit = size_t(-1))
{
    DBGPRINT("%s: begin: wname %s", Q_FUNC_INFO, wname);
    for (size_t i = 0; widget != nullptr && i < limit; ++i) {
        const QMetaObject *mo = widget->metaObject();
        while (mo && std::strcmp(mo->className(), wname) != 0) {
            mo = mo->superClass();
        }

        if (mo != nullptr) {
            DBGPRINT("%s: yeah, it(%s) is %s", Q_FUNC_INFO,
                     qPrintable(widget->objectName()), wname);
            return widget;
        } else {
            widget = qobject_cast<QWidget *>(widget->parent());
        }
    }
    return nullptr;
}

static QString qtreeWidgetActivateClick(const EventInfo &eventInfo)
{
    static TreeWidgetWatcher treeWidgetWatcher(eventInfo.codeGenerator);

    QString res;
    QEvent *event = eventInfo.event;
    QWidget *widget = eventInfo.widget;
    if (widget == nullptr || event == nullptr)
        return res;

    if (event->type() == QEvent::MouseButtonRelease) {
        treeWidgetWatcher.disconnectAll();
        return res;
    }

    if (!(event->type() == QEvent::MouseButtonDblClick
          || event->type() == QEvent::MouseButtonPress))
        return res;

    auto mouseEvent = static_cast<QMouseEvent *>(event);
    const QPoint pos = widget->mapFromGlobal(mouseEvent->globalPos());

    QWidget *treeWidget
        = searchThroghSuperClassesAndParents(widget, "QTreeWidget", 2);

    if (treeWidget == nullptr
        || (widget != treeWidget
            && qobject_cast<QWidget *>(widget->parent()) != treeWidget))
        return res;

    DBGPRINT("%s: yeah, it is tree widget", Q_FUNC_INFO);
    QTreeWidget *tw = qobject_cast<QTreeWidget *>(treeWidget);
    assert(tw != nullptr);
    QTreeWidgetItem *twi = tw->itemAt(pos);
#ifdef DEBUG_ANALYZER
    QRect tir = tw->visualItemRect(twi);
#endif
    DBGPRINT("%s: tir.x %d, tir.y %d, pos.x %d, pos.y %d, %s", Q_FUNC_INFO,
             tir.x(), tir.y(), pos.x(), pos.y(),
             mouseEvent->type() == QEvent::MouseButtonDblClick ? "double click"
                                                               : "click");
    if (twi != nullptr) {
        QString text = twi->text(0);
        if (!text.isEmpty()) {
            text.replace(QChar('\n'), "\\n");
            DBGPRINT("%s: QtreeWidget text %s", Q_FUNC_INFO, qPrintable(text));
            if (mouseEvent->type() == QEvent::MouseButtonDblClick)
                res = QStringLiteral("Test.doubleClickItem('%1', '%2');")
                          .arg(qt_monkey_agent::fullQtWidgetId(*tw))
                          .arg(text);
            else
                res = QStringLiteral("Test.activateItem('%1', '%2');")
                          .arg(qt_monkey_agent::fullQtWidgetId(*tw))
                          .arg(text);

            if (treeWidgetWatcher.watch(tw)) { // new one
                QObject::connect(tw, SIGNAL(itemExpanded(QTreeWidgetItem *)),
                                 &treeWidgetWatcher,
                                 SLOT(itemExpanded(QTreeWidgetItem *)));
                QObject::connect(tw, SIGNAL(destroyed(QObject *)),
                                 &treeWidgetWatcher,
                                 SLOT(treeWidgetDestroyed(QObject *)));
            }
        }
    }
    return res;
}

static QString qcomboBoxActivateClick(const EventInfo &eventInfo)
{
    QString res;
    QEvent *event = eventInfo.event;
    QWidget *widget = eventInfo.widget;
    if (widget == nullptr || event == nullptr
        || !(event->type() == QEvent::MouseButtonDblClick
             || event->type() == QEvent::MouseButtonPress))
        return res;

    auto mouseEvent = static_cast<QMouseEvent *>(event);
    const QPoint pos = widget->mapFromGlobal(mouseEvent->globalPos());

    if (QWidget *combobox
        = searchThroghSuperClassesAndParents(widget, "QComboBox")) {
        DBGPRINT("%s: this is combobox", Q_FUNC_INFO);
        if (widget == combobox)
            return res;
        auto lv = qobject_cast<QListView *>(widget);
        if (lv == nullptr && widget->parent() != nullptr)
            lv = qobject_cast<QListView *>(widget->parent());
        if (lv == nullptr)
            return res;
        DBGPRINT("%s catch press on QListView falldown list", Q_FUNC_INFO);
        const QModelIndex idx = lv->indexAt(pos);
        DBGPRINT("%s: row %d", Q_FUNC_INFO, idx.row());
        return QStringLiteral("Test.activateItem('%1', '%2');")
            .arg(qt_monkey_agent::fullQtWidgetId(*combobox))
            .arg(qobject_cast<QComboBox *>(combobox)->itemText(idx.row()));
    } else if (QWidget *alistwdg
               = searchThroghSuperClassesAndParents(widget, "QListWidget")) {
        DBGPRINT("%s: this is QListWidget", Q_FUNC_INFO);
        auto listwdg = qobject_cast<QListWidget *>(alistwdg);
        if (listwdg == nullptr)
            return res;
        const QListWidgetItem *it = listwdg->itemAt(pos);
        if (it == nullptr)
            return res;
        return QStringLiteral("Test.activateItem('%1', '%2');")
            .arg(qt_monkey_agent::fullQtWidgetId(*listwdg))
            .arg(it->text());
    }
    return res;
}

static QString qlistWidgetActivateClick(const EventInfo &eventInfo)
{
    QString res;
    QWidget *widget = eventInfo.widget;
    QEvent *event = eventInfo.event;
    if (widget == nullptr || event == nullptr
        || !(event->type() == QEvent::MouseButtonDblClick
             || event->type() == QEvent::MouseButtonPress))
        return res;

    auto mouseEvent = static_cast<QMouseEvent *>(event);
    const QPoint pos = widget->mapFromGlobal(mouseEvent->globalPos());

    if (QWidget *alistwdg
        = searchThroghSuperClassesAndParents(widget, "QListWidget")) {
        DBGPRINT("%s: this is QListWidget", Q_FUNC_INFO);
        QListWidget *listwdg = qobject_cast<QListWidget *>(alistwdg);
        if (listwdg == nullptr)
            return res;
        QListWidgetItem *it = listwdg->itemAt(pos);
        if (it == nullptr)
            return res;
        return QStringLiteral("Test.activateItem('%1', '%2');")
            .arg(qt_monkey_agent::fullQtWidgetId(*listwdg))
            .arg(it->text());
    }
    return res;
}

static QString qtabBarActivateClick(const EventInfo &eventInfo)
{
    QString res;
    QEvent *event = eventInfo.event;
    QWidget *widget = eventInfo.widget;
    if (widget == nullptr || event == nullptr
        || !(event->type() == QEvent::MouseButtonDblClick
             || event->type() == QEvent::MouseButtonPress))
        return res;
    auto mouseEvent = static_cast<QMouseEvent *>(event);
    const QPoint pos = widget->mapFromGlobal(mouseEvent->globalPos());

    auto tabBar = qobject_cast<QTabBar *>(widget);
    if (tabBar == nullptr)
        return res;
    const int tab = tabBar->tabAt(pos);
    if (tab == -1) {
        DBGPRINT("%s: not tab at pos %d %d", Q_FUNC_INFO, pos.x(), pos.y());
        return res;
    }
    return QStringLiteral("Test.activateItem('%1', '%2');")
        .arg(qt_monkey_agent::fullQtWidgetId(*tabBar))
        .arg(tabBar->tabText(tab));
}

static QString modelIndexToPos(const QModelIndex &mi)
{
    std::deque<std::pair<int, int>> idxs;
    QModelIndex null_idx = QModelIndex();
    QModelIndex cur_idx = mi;

    do {
        idxs.push_back(std::make_pair(cur_idx.column(), cur_idx.row()));
        cur_idx = cur_idx.parent();
    } while (cur_idx != null_idx);

    QString res = "[";
    for (auto it = idxs.rbegin(); it != idxs.rend(); ++it)
        res += QStringLiteral("%1, %2,").arg(it->first).arg(it->second);
    // last ','
    res[res.size() - 1] = ']';
    return res;
}

static QString qtreeViewActivateClick(const EventInfo &eventInfo)
{
    QString res;
    QWidget *widget = eventInfo.widget;
    QEvent *event = eventInfo.event;
    if (widget == nullptr || event == nullptr)
        return res;

    static TreeViewWatcher watcher(eventInfo.codeGenerator);

    if (event->type() == QEvent::MouseButtonRelease) {
        watcher.disconnectAll();
        return res;
    }

    if (!(event->type() == QEvent::MouseButtonDblClick
          || event->type() == QEvent::MouseButtonPress))
        return res;

    auto mouseEvent = static_cast<QMouseEvent *>(event);
    const QPoint pos = widget->mapFromGlobal(mouseEvent->globalPos());

    QWidget *tree_view
        = searchThroghSuperClassesAndParents(widget, "QTreeView", 2);
    if (tree_view == nullptr)
        return res;

    if (widget == tree_view
        || qobject_cast<QWidget *>(widget->parent()) == tree_view) {
        DBGPRINT("%s: yeah, it is tree view", Q_FUNC_INFO);
        QTreeView *tv = qobject_cast<QTreeView *>(tree_view);
        if (tv == nullptr)
            return res;
        QModelIndex mi = tv->indexAt(pos);
        if (mi.isValid()) {
            DBGPRINT("%s: column %d, row %d, have parent %s", Q_FUNC_INFO,
                     mi.column(), mi.row(),
                     mi.parent() == QModelIndex() ? "false" : "true");
            if (mouseEvent->type() == QEvent::MouseButtonDblClick)
                res = QStringLiteral("Test.doubleClickOnItemInView('%1', %2);")
                          .arg(qt_monkey_agent::fullQtWidgetId(*tv))
                          .arg(modelIndexToPos(mi));
            else
                res = QStringLiteral("Test.activateItemInView('%1', %2);")
                          .arg(qt_monkey_agent::fullQtWidgetId(*tv))
                          .arg(modelIndexToPos(mi));
            watcher.watch(*tv);
        } else {
            DBGPRINT("%s: not valid model index for tv", Q_FUNC_INFO);
        }
    }
    return res;
}

static QString qlistViewActivateClick(const EventInfo &eventInfo)
{
    QString res;
    QEvent *event = eventInfo.event;
    QWidget *widget = eventInfo.widget;
    if (widget == nullptr || event == nullptr
        || !(event->type() == QEvent::MouseButtonDblClick
             || event->type() == QEvent::MouseButtonPress))
        return res;
    auto mouseEvent = static_cast<QMouseEvent *>(event);
    const QPoint pos = widget->mapFromGlobal(mouseEvent->globalPos());
    QWidget *listView
        = searchThroghSuperClassesAndParents(widget, "QListView", 2);
    if (listView == nullptr)
        return res;

    if (widget == listView
        || qobject_cast<QWidget *>(widget->parent()) == listView) {
        DBGPRINT("%s: yeah, it is list view", Q_FUNC_INFO);
        QListView *lv = qobject_cast<QListView *>(listView);
        if (lv == nullptr)
            return res;
        QModelIndex mi = lv->indexAt(pos);
        if (mi.isValid() && !mi.data().toString().isEmpty()) {
            DBGPRINT("%s: column %d, row %d, have parent %s", Q_FUNC_INFO,
                     mi.column(), mi.row(),
                     mi.parent() == QModelIndex() ? "false" : "true");
            QString text = mi.data().toString();
            text.replace(QChar('\n'), "\\n");
            text.replace(QChar('\''), "\\'");
            res = QStringLiteral("Test.activateItem('%1', '%2');")
                      .arg(qt_monkey_agent::fullQtWidgetId(*lv))
                      .arg(text);

        } else {
            DBGPRINT("%s: not valid model index for lv", Q_FUNC_INFO);
        }
    }
    return res;
}

static QString workspaceTitleBarPressed(const EventInfo &eventInfo)
{
    QString res;
    QEvent *event = eventInfo.event;
    if (event->type() != QEvent::MouseButtonPress)
        return res;
    auto mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() != Qt::LeftButton)
        return res;
    QWidget *w = QApplication::widgetAt(mouseEvent->globalPos());
    if (w == nullptr
        || std::strcmp(w->metaObject()->className(), "QWorkspaceTitleBar") != 0
        || w->parent() == nullptr
        || std::strcmp(w->parent()->metaObject()->className(),
                       "QWorkspaceChild")
               != 0
        || w->parent()->parent() == nullptr
        || std::strcmp(w->parent()->parent()->metaObject()->className(),
                       "QWorkspace")
               != 0)
        return res;

    w = qobject_cast<QWidget *>(w->parent());
    auto parent = qobject_cast<QWidget *>(w->parent());
    if (w == nullptr || parent == nullptr)
        return res;
    return QStringLiteral("Test.chooseWindowWithTitle('%1', '%2');")
        .arg(qt_monkey_agent::fullQtWidgetId(*parent))
        .arg(w->windowTitle());
}

static QString clickOnUnnamedButton(const EventInfo &eventInfo)
{
    QString res;
    QEvent *event = eventInfo.event;
    if (event->type() != QEvent::MouseButtonPress)
        return res;
    auto mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() != Qt::LeftButton)
        return res;
    QWidget *w = QApplication::widgetAt(mouseEvent->globalPos());
    QAbstractButton *bt;
    if (w == nullptr || (bt = qobject_cast<QAbstractButton *>(w)) == nullptr)
        return res;
    if (!bt->objectName().isEmpty() || bt->text().isEmpty()
        || bt->parent() == nullptr)
        return res;
    QString text = bt->text();
    qt_monkey_agent::escapeTextForScript(text);
    auto parent = qobject_cast<QWidget *>(bt->parent());
    if (parent == nullptr)
        return res;
    return QStringLiteral("Test.pressButtonWithText('%1', '%2');")
        .arg(qt_monkey_agent::fullQtWidgetId(*parent))
        .arg(text);
}

#ifdef Q_OS_MAC
static QString qmenuOnMacTriggered(const EventInfo &eventInfo)
{
    static MacMenuActionWatcher watcher(eventInfo.codeGenerator);
    QEvent *event = eventInfo.event;
    QObject *obj = eventInfo.obj;
    QString res;
    if (!(event->type() == QEvent::ActionAdded
          || event->type() == QEvent::ActionChanged
          || event->type() == QEvent::ActionRemoved)
        || obj == nullptr)
        return res;
    auto actEvent = static_cast<QActionEvent *>(event);
    QAction *action = actEvent->action();
    auto menuOwnerWidget = qobject_cast<QWidget *>(obj);
    if (action == nullptr
        || (event->type() == QEvent::ActionAdded && menuOwnerWidget == nullptr))
        return res;
    DBGPRINT("%s: !!!MAC!!! %s[%s], text %s, type %s", Q_FUNC_INFO,
             qPrintable(obj->objectName()), obj->metaObject()->className(),
             qPrintable(actEvent->action()->text()),
             event->type() == QEvent::ActionAdded
                 ? "add"
                 : event->type() == QEvent::ActionChanged ? "change"
                                                          : "remove");
    if (event->type() == QEvent::ActionAdded) {
        QString menuOwnerName
            = qt_monkey_agent::fullQtWidgetId(*menuOwnerWidget);
        watcher.startWatch(eventInfo.agent, *action, std::move(menuOwnerName));
    } else if (event->type() == QEvent::ActionRemoved) {
        watcher.stopWatch(eventInfo.agent, *action);
    }
    return res;
}
#else
static QString qmenuOnMacTriggered(const EventInfo &) { return QString(); }
#endif

static const std::pair<Qt::MouseButton, QLatin1String> mouseBtnNames[] = {
    {Qt::LeftButton, QLatin1String("Qt.LeftButton")},
    {Qt::RightButton, QLatin1String("Qt.RightButton")},
    {Qt::MidButton, QLatin1String("Qt.MidButton")},
};

static QString widgetUnderCursorInfo()
{
    QWidget *w = QApplication::widgetAt(QCursor::pos());

    QString res = QStringLiteral("Widget at cursor info:\n");

    QWidget *win = qApp->activeModalWidget();
    if (win)
        res += QStringLiteral("Modal Widget %1\n").arg(win->objectName());
    else
        res += QStringLiteral("Modal Windget null\n");

    win = qApp->activePopupWidget();
    if (win)
        res += QStringLiteral("Popup Widget %1\n").arg(win->objectName());
    else
        res += QStringLiteral("Popup Window nullptr\n");

    win = qApp->activeWindow();
    if (win)
        res += QStringLiteral("Active Widget %1\n").arg(win->objectName());
    else
        res += QStringLiteral("Active Widget nullptr\n");

    if (w == nullptr)
        return res;

    const QObjectList &childs = w->children();

    res += QStringLiteral("class name %1, object name %2\n")
               .arg(w->metaObject()->className())
               .arg(w->objectName());
    QObject *obj = w->parent();
    while (obj != nullptr) {
        res += QStringLiteral("parent class name %1, object name %2\n")
                   .arg(obj->metaObject()->className())
                   .arg(obj->objectName());
        obj = obj->parent();
    }
    for (QObject *child : childs) {
        res += QStringLiteral("child class name %1, object name %2\n")
                   .arg(child->metaObject()->className())
                   .arg(child->objectName());
    }
    res += QStringLiteral("Object id: %1\n")
               .arg(qt_monkey_agent::fullQtWidgetId(*w));
    res += QStringLiteral("Widget at cursor info END\n");
    return res;
}

} // namespace {

void qt_monkey_agent::escapeTextForScript(QString &text)
{
    text.replace(QChar('\n'), "\\n");
}

bool qt_monkey_agent::stringToMouseButton(const QString &str,
                                          Qt::MouseButton &bt)
{
    for (auto &&elm : mouseBtnNames)
        if (elm.second == str) {
            bt = elm.first;
            return true;
        }

    return false;
}

QString qt_monkey_agent::mouseButtonEnumToString(Qt::MouseButton b)
{

    for (auto &&elm : mouseBtnNames)
        if (elm.first == b)
            return elm.second;

    return QLatin1String("<unknown button>");
}

QString qt_monkey_agent::fullQtWidgetId(const QObject &w)
{
    QString res = qtObjectId(w);
    DBGPRINT("%s: class name %s, id %s", Q_FUNC_INFO,
             w.metaObject()->className(), qPrintable(res));
    QObject *cur_obj = w.parent();
    while (cur_obj != nullptr) {
        res = qtObjectId(*cur_obj) + "." + res;
        cur_obj = cur_obj->parent();
    }
    return res;
}

UserEventsAnalyzer::UserEventsAnalyzer(
    qt_monkey_agent::Agent &agent, const QKeySequence &showObjectShortCut,
    std::list<CustomEventAnalyzer> customEventAnalyzers, QObject *parent)
    : QObject(parent), agent_(agent),
      customEventAnalyzers_(std::move(customEventAnalyzers)),
      generateScriptCmd_(
          [this](QString code) { emit userEventInScriptForm(code); }),
      showObjectShortCut_(showObjectShortCut)
{
    for (auto &&fun :
         {qmenuActivateClick, qtreeWidgetActivateClick, qcomboBoxActivateClick,
          qlistWidgetActivateClick, qtabBarActivateClick,
          qtreeViewActivateClick, qlistViewActivateClick,
          workspaceTitleBarPressed, clickOnUnnamedButton, qmenuOnMacTriggered})
        customEventAnalyzers_.emplace_back(fun);
}

QString
UserEventsAnalyzer::callCustomEventAnalyzers(QObject *obj, QEvent *event,
                                             QWidget *widget,
                                             const QString &widgetName) const
{
    QString code;
    for (auto &&userCustomAnalyzer : customEventAnalyzers_) {
        code = userCustomAnalyzer(
            {agent_, obj, event, widget, widgetName, generateScriptCmd_});
        if (!code.isEmpty())
            return code;
    }
    return code;
}

bool UserEventsAnalyzer::alreadySawSuchKeyEvent(QKeyEvent *keyEvent)
{
    const QDateTime now = QDateTime::currentDateTime();
    if (lastKeyEvent_.type == keyEvent->type()
        && keyEvent->key() == lastKeyEvent_.key
        && std::llabs(now.msecsTo(lastKeyEvent_.timestamp))
               < repeatEventTimeoutMs) {
        DBGPRINT("%s: we saw it already", Q_FUNC_INFO);
        return true;
    }

    lastKeyEvent_.type = keyEvent->type();
    lastKeyEvent_.timestamp = now;
    lastKeyEvent_.key = keyEvent->key();

    if (keyEvent->type() == QEvent::KeyPress) {
        ++keyPress_;
        DBGPRINT("%s: Press ev %p, keypress %zu", Q_FUNC_INFO, keyEvent,
                 keyPress_);
    } else if (keyEvent->type() == QEvent::KeyRelease) {
        ++keyRelease_;
        DBGPRINT("%s: Release ev %p, keyPress %zu, keyRelease %zu", Q_FUNC_INFO,
                 keyEvent, keyPress_, keyRelease_);
        if (keyPress_ == keyRelease_) {
            DBGPRINT("%s: we already see it via key press", Q_FUNC_INFO);
            return true;
        }
        keyPress_ = keyRelease_ = 0;
    }
    return false;
}

bool UserEventsAnalyzer::alreadySawSuchMouseEvent(const QString &widgetName,
                                                  QMouseEvent *mouseEvent)
{
    const QDateTime now = QDateTime::currentDateTime();
    DBGPRINT("widgetName %s, last name %s, %s", qPrintable(widgetName),
             qPrintable(lastMouseEvent_.widgetName),
             widgetName == lastMouseEvent_.widgetName ? "true" : "false");
    DBGPRINT("1 %s 2 %lld 3 %s 4 %s",
             mouseEvent->type() == lastMouseEvent_.type ? "t" : "f",
             std::llabs(now.msecsTo(lastMouseEvent_.timestamp)),
             mouseEvent->globalPos() == lastMouseEvent_.globalPos ? "t" : "f",
             mouseEvent->buttons() == lastMouseEvent_.buttons ? "t" : "f");
    if (mouseEvent->type() == lastMouseEvent_.type
        && std::llabs(now.msecsTo(lastMouseEvent_.timestamp))
               < repeatEventTimeoutMs
        && mouseEvent->globalPos() == lastMouseEvent_.globalPos
        && mouseEvent->buttons() == lastMouseEvent_.buttons
        && lastMouseEvent_.widgetName == widgetName) {
        DBGPRINT("%s: true", Q_FUNC_INFO);
        return true;
    }
    lastMouseEvent_.type = mouseEvent->type();
    lastMouseEvent_.timestamp = now;
    lastMouseEvent_.globalPos = mouseEvent->globalPos();
    lastMouseEvent_.buttons = mouseEvent->buttons();
    lastMouseEvent_.widgetName = widgetName;
    DBGPRINT("%s: false", Q_FUNC_INFO);
    return false;
}

bool UserEventsAnalyzer::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        DBGPRINT("%s: key event for '%s'\n", Q_FUNC_INFO,
                 qPrintable(obj->objectName()));
        auto keyEvent = static_cast<QKeyEvent *>(event);

        if (alreadySawSuchKeyEvent(keyEvent))
            break;

        // ignore special keys alone
        if (keyEvent->key() == Qt::Key_Shift || keyEvent->key() == Qt::Key_Alt
            || keyEvent->key() == Qt::Key_Control
            || keyEvent->key() == Qt::Key_Meta) {
            DBGPRINT("%s: special key alone", Q_FUNC_INFO);
            break;
        }
        const QKeySequence curKey{keyEvent->key()
                                  | static_cast<int>(keyEvent->modifiers())};
        if (keyEvent->type() == QEvent::KeyPress
            && curKey == showObjectShortCut_) {
            emit scriptLog(widgetUnderCursorInfo());
            break;
        }
        QWidget *w = QApplication::focusWidget();
        if (w == nullptr) {
            w = QApplication::widgetAt(QCursor::pos());
            if (w == nullptr) {
                DBGPRINT("(%s, %d): Can not find out what widget is used!!!",
                         Q_FUNC_INFO, __LINE__);
                break;
            }
        }
        const QString widgetName = qt_monkey_agent::fullQtWidgetId(*w);
        QString scriptLine
            = callCustomEventAnalyzers(obj, event, w, widgetName);
        if (scriptLine.isEmpty())
            scriptLine = keyReleaseEventToScript(widgetName, keyEvent);
        DBGPRINT("%s: we emit '%s'", Q_FUNC_INFO, qPrintable(scriptLine));
        emit userEventInScriptForm(scriptLine);
        break;
    }
    case QEvent::MouseButtonRelease:
        lastMouseEvent_.type = QEvent::None;
        break;
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonPress: {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        DBGPRINT("%s: mouse event(%p) for %p'%s'[%s]: %s\n", Q_FUNC_INFO, event,
                 obj, qPrintable(obj->objectName()),
                 qPrintable(obj->metaObject()->className()),
                 event->type() == QEvent::MouseButtonDblClick ? "double click"
                                                              : "press event");
        const QPoint clickPos = mouseEvent->globalPos();
        QWidget *w = QApplication::widgetAt(clickPos);
        if (w == nullptr) {
            DBGPRINT("(%s, %d): Can not find out what widget is used(x %d, y "
                     "%d, w %p, tw %p)!!!",
                     Q_FUNC_INFO, __LINE__, clickPos.x(), clickPos.y(),
                     QApplication::widgetAt(clickPos),
                     QApplication::topLevelAt(clickPos));
            return false;
        }

        QString widgetName = fullQtWidgetId(*w);
        if (alreadySawSuchMouseEvent(widgetName, mouseEvent))
            break;
        QString scriptLine
            = callCustomEventAnalyzers(obj, event, w, widgetName);
        QPoint pos = w->mapFromGlobal(clickPos);
        if (scriptLine.isEmpty())
            scriptLine = mouseEventToJavaScript(widgetName, mouseEvent, pos);

        if (w->objectName().isEmpty() && !isOnlyOneChildWithSuchClass(*w)) {
            QWidget *baseWidget = w;
            while (w != nullptr && w->objectName().isEmpty())
                w = qobject_cast<QWidget *>(w->parent());
            if (w != nullptr && w != baseWidget) {
                pos = w->mapFromGlobal(clickPos);
                widgetName = fullQtWidgetId(*w);
                if (alreadySawSuchMouseEvent(widgetName, mouseEvent))
                    return QObject::eventFilter(obj, event);
                QString anotherScript
                    = callCustomEventAnalyzers(obj, event, w, widgetName);
                if (anotherScript.isEmpty())
                    anotherScript
                        = mouseEventToJavaScript(widgetName, mouseEvent, pos);
                if (scriptLine != anotherScript)
                    scriptLine = QString("%1\n//another variant:%2")
                                     .arg(scriptLine)
                                     .arg(anotherScript);
            }
        }
        DBGPRINT("%s: emit userEventInScriptForm", Q_FUNC_INFO);
        emit userEventInScriptForm(scriptLine);
        break;
    } // event by mouse
    case QEvent::Shortcut: {
        DBGPRINT(
            "%s: shortcut event: %s to %s", Q_FUNC_INFO,
            qPrintable(static_cast<QShortcutEvent *>(event)->key().toString()),
            qPrintable(qt_monkey_agent::fullQtWidgetId(*obj)));
    }
    // fall through
    default: {
        const QString code
            = callCustomEventAnalyzers(obj, event, nullptr, QString());
        if (!code.isEmpty())
            emit userEventInScriptForm(code);
        break;
    }
    } // switch (event->type())
    return QObject::eventFilter(obj, event);
}

void TreeWidgetWatcher::itemExpanded(QTreeWidgetItem *twi)
{
    DBGPRINT("%s begin", Q_FUNC_INFO);
    assert(twi != nullptr);
    QTreeWidget *tw = twi->treeWidget();
    assert(tw != nullptr);
    generateScriptCmd_(QStringLiteral("Test.expandItemInTree('%1', '%2');")
                           .arg(fullQtWidgetId(*tw))
                           .arg(twi->text(0)));

    disconnect(tw, SIGNAL(itemExpanded(QTreeWidgetItem *)), this,
               SLOT(itemExpanded(QTreeWidgetItem *)));
    auto it = treeWidgetsSet_.find(tw);
    assert(it != treeWidgetsSet_.end());
    treeWidgetsSet_.erase(it);
}

void TreeWidgetWatcher::treeWidgetDestroyed(QObject *obj)
{
    DBGPRINT("begin %s", Q_FUNC_INFO);
    assert(obj != nullptr);
    auto it = treeWidgetsSet_.find(obj);
    if (it != treeWidgetsSet_.end())
        treeWidgetsSet_.erase(it);
}

bool TreeWidgetWatcher::watch(QTreeWidget *tw)
{
    assert(tw != nullptr);
    return treeWidgetsSet_.insert(tw).second;
}

void TreeWidgetWatcher::disconnectAll()
{
    for (QObject *obj : treeWidgetsSet_) {
        QObject::disconnect(obj, SIGNAL(itemExpanded(QTreeWidgetItem *)), this,
                            SLOT(itemExpanded(QTreeWidgetItem *)));
        QObject::disconnect(obj, SIGNAL(destroyed(QObject *)), this,
                            SLOT(treeWidgetDestroyed(QObject *)));
    }
    treeWidgetsSet_.clear();
}

void TreeViewWatcher::treeViewExpanded(const QModelIndex &index)
{
    DBGPRINT("%s begin", Q_FUNC_INFO);
    auto it = modelToView_.find(index.model());
    assert(it != modelToView_.end());
    auto tv = qobject_cast<const QTreeView *>(it->second);
    assert(tv != nullptr);
    generateScriptCmd_(QStringLiteral("Test.expandItemInTreeView('%1', %2);")
                           .arg(qt_monkey_agent::fullQtWidgetId(*tv))
                           .arg(modelIndexToPos(index)));
    disconnect(tv, SIGNAL(expanded(const QModelIndex &)), this,
               SLOT(treeViewExpanded(const QModelIndex &)));
    auto jt = treeViewSet_.find(tv);
    assert(jt != treeViewSet_.end());
    treeViewSet_.erase(jt);
}

void TreeViewWatcher::treeViewDestroyed(QObject *obj)
{
    DBGPRINT("begin %s", Q_FUNC_INFO);
    assert(obj != nullptr);
    auto it = treeViewSet_.find(obj);
    if (it != treeViewSet_.end())
        treeViewSet_.erase(it);

    for (auto it = modelToView_.begin(); it != modelToView_.end(); ++it)
        if (it->second == obj) {
            modelToView_.erase(it);
            break;
        }
}

void TreeViewWatcher::watch(QTreeView &tv)
{
    if (treeViewSet_.insert(&tv).second) { // we insert something
        if (tv.model() == nullptr)
            return;
        modelToView_[tv.model()] = &tv;
        connect(&tv, SIGNAL(expanded(const QModelIndex &)), this,
                SLOT(treeViewExpanded(const QModelIndex &)));
        connect(&tv, SIGNAL(destroyed(QObject *)), this,
                SLOT(treeViewDestroyed(QObject *)));
    }
}

void TreeViewWatcher::disconnectAll()
{
    for (const QObject *obj : treeViewSet_) {
        disconnect(obj, SIGNAL(expanded(const QModelIndex &)), this,
                   SLOT(treeViewExpanded(const QModelIndex &)));
        disconnect(obj, SIGNAL(destroyed(QObject *)), this,
                   SLOT(treeViewDestroyed(QObject *)));
    }
    treeViewSet_.clear();
    modelToView_.clear();
}

#ifdef Q_OS_MAC
void MacMenuActionWatcher::startWatch(qt_monkey_agent::Agent &agent,
                                      QAction &act, QString menuName)
{
    auto insertRes = actions_.emplace(&act, menuName);
    if (insertRes.second) { // we really insert
        connect(&act, SIGNAL(triggered(bool)), this, SLOT(onTriggered()));
        auto ptr = agent.menuItemsOnMac_.get();
        ptr->emplace(std::move(menuName), &act);
    }
}

void MacMenuActionWatcher::stopWatch(qt_monkey_agent::Agent &agent,
                                     QAction &act)
{
    auto it = actions_.find(&act);
    if (it != actions_.end())
        actions_.erase(it);
    {
        auto ptr = agent.menuItemsOnMac_.get();
        for (auto it = ptr->begin(), endi = ptr->end(); it != endi; ++it)
            if (it->second == &act) {
                ptr->erase(it);
                return;
            }
    }
}

void MacMenuActionWatcher::onTriggered()
{
    auto obj = sender();
    if (obj == nullptr)
        return;
    DBGPRINT("%s: you press something: %s", Q_FUNC_INFO,
             qPrintable(obj->objectName()));
    auto act = qobject_cast<QAction *>(obj);
    if (act == nullptr)
        return;
    auto it = actions_.find(act);
    if (it != actions_.end())
        generateScriptCmd_(QStringLiteral("Test.activateItem('%1', '%2');")
                               .arg(it->second)
                               .arg(act->text()));
}
#else
void MacMenuActionWatcher::onTriggered() {}
#endif
