//#define DEBUG_SCRIPT_API
#include "script_api.hpp"

#include <cassert>
#include <chrono>
#include <thread>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QStyleOption>
#include <QTreeWidget>
#include <QWidget>
#if QT_VERSION < 0x050000
#include <QWorkspace>
#else
#include <QMdiArea>
#include <QMdiSubWindow>
#define QWorkspace QMdiArea
#endif
#include <QtCore/QStringList>
#include <QtScript/QScriptEngine>
#include <QtTest/QTest>

#include "agent.hpp"
#include "common.hpp"
#include "script_runner.hpp"
#include "user_events_analyzer.hpp"

using qt_monkey_agent::ScriptAPI;
using qt_monkey_agent::Agent;

#ifdef DEBUG_SCRIPT_API
#define DBGPRINT(fmt, ...) qDebug(fmt, __VA_ARGS__)
#else
#define DBGPRINT(fmt, ...)                                                     \
    do {                                                                       \
    } while (false)
#endif

namespace
{
static const int sleepTimeForWaitWidgetMs = 70;

class MyLineEdit final : public QLineEdit
{
public:
    static QRect adjustedContentsRect(const QLineEdit &le);
};

QRect MyLineEdit::adjustedContentsRect(const QLineEdit &le)
{
    QStyleOptionFrameV2 opt;
    reinterpret_cast<const MyLineEdit &>(le).initStyleOption(&opt);
    return le.style()->subElementRect(QStyle::SE_LineEditContents, &opt, &le);
}
static QWidget *bruteForceWidgetSearch(const QString &objectName,
                                       const QString &className,
                                       bool shouldBeEnabled)
{
    for (QWidget *widget : QApplication::allWidgets()) {
        if (!className.isEmpty()
            && className == widget->metaObject()->className()) {
            DBGPRINT("%s: found widget with class %s, w %s", Q_FUNC_INFO,
                     qPrintable(className),
                     widget == nullptr ? "null" : "not null");
            if (shouldBeEnabled
                && !(widget->isEnabled() && widget->isVisible()))
                continue;
            return widget;
        }
        if (widget->objectName() == objectName) {
            DBGPRINT("%s: found widget %s", Q_FUNC_INFO,
                     qPrintable(objectName));
            if (shouldBeEnabled
                && !(widget->isEnabled() && widget->isVisible()))
                continue;
            return widget;
        }
    }

    DBGPRINT("(%s, %d): brute force do not give any suitable result",
             Q_FUNC_INFO, __LINE__);
    return nullptr;
}

static QWidget *doGetWidgetWithSuchName(const QString &objectName,
                                        bool shouldBeEnabled)
{
    QStringList names = objectName.split('.');

    if (names.isEmpty()) {
        DBGPRINT("%s: list of widget's name empty\n", Q_FUNC_INFO);
        return nullptr;
    }

    QWidget *win = qApp->activeModalWidget();

    DBGPRINT("(%s, %d): Modal Window %s", Q_FUNC_INFO, __LINE__,
             win != nullptr ? qPrintable(win->objectName()) : "nullptr");

    win = qApp->activePopupWidget();
    DBGPRINT("(%s, %d): popup Window %s", Q_FUNC_INFO, __LINE__,
             win != nullptr ? qPrintable(win->objectName()) : "nullptr");

    win = qApp->activeWindow();
    DBGPRINT("(%s, %d): active Window %s", Q_FUNC_INFO, __LINE__,
             win != nullptr ? qPrintable(win->objectName()) : "nullptr");

    const QString &mainWidgetName = names.first();
    DBGPRINT("(%s, %d): search widget with such name %s", Q_FUNC_INFO, __LINE__,
             qPrintable(mainWidgetName));
    QList<QObject *> lst
        = QCoreApplication::instance()->findChildren<QObject *>(mainWidgetName);
    if (lst.isEmpty()) {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (mainWidgetName == widget->objectName()) {
                lst << widget;
                break;
            } else {
                lst = widget->findChildren<QObject *>(mainWidgetName);
            }

            if (!lst.isEmpty())
                break;
        }
    }
    const QRegExp class_name_rx("^<class_name=([^>]+)>$");
    if (lst.isEmpty()) {
        DBGPRINT("%s: list of widget's name empty, start bruteforce\n",
                 Q_FUNC_INFO);
        //! \todo if there is class name use it in brute search
        QString className;
        if (class_name_rx.indexIn(mainWidgetName) != -1)
            className = class_name_rx.cap(1);
        QWidget *w = bruteForceWidgetSearch(mainWidgetName, className,
                                            shouldBeEnabled);
        if (w == nullptr)
            return nullptr;
        lst << w;
    }

    //! \todo may be try all variants, instead of lst.first?
    QWidget *w = qobject_cast<QWidget *>(lst.first());
    assert(w != nullptr);
    DBGPRINT("%s: we found %s", Q_FUNC_INFO, qPrintable(w->objectName()));
    names.removeFirst();

    while (!names.isEmpty()) {
        const QObjectList &clist = w->children();
        const QString &el = names.first();
        QString class_name;
        int class_order = 0;
        if (class_name_rx.indexIn(el) != -1) {
            class_name = class_name_rx.cap(1);
            const QStringList res
                = class_name.split(",", QString::SkipEmptyParts);
            if (res.size() == 0)
                class_name = "";
            else
                class_name = res[0];
            if (res.size() > 1) {
                bool ok = false;
                class_order = res[1].toInt(&ok);
                if (!ok)
                    class_order = 0;
            }
            DBGPRINT("%s: search object with class: %s, order %d", Q_FUNC_INFO,
                     qPrintable(class_name), class_order);
        }
        int order = 0;
        QObjectList::const_iterator it;
        for (it = clist.begin(); it != clist.end(); ++it) {
            if ((!class_name.isEmpty()
                 && class_name == (*it)->metaObject()->className())
                || (*it)->objectName() == el) {
                DBGPRINT("%s: found widget %s, order %d", Q_FUNC_INFO,
                         qPrintable(el), order);
                w = qobject_cast<QWidget *>(*it);
                if (shouldBeEnabled && !(w->isVisible() && w->isEnabled()))
                    continue;
                if (order++ != class_order)
                    continue;

                names.removeFirst();
                break;
            }
        }

        if (it == clist.end()) {
            DBGPRINT("(%s, %d): Can not find object with such name %s, try "
                     "brute search",
                     Q_FUNC_INFO, __LINE__, qPrintable(el));
            w = bruteForceWidgetSearch(el, class_name, shouldBeEnabled);
            if (w == nullptr) {
                DBGPRINT("(%s, %d) brute force failed", Q_FUNC_INFO, __LINE__);
                return nullptr;
            }
            names.removeFirst();
        }
    }

    return w;
}

static Qt::MatchFlag matchFlagFromString(const QString &flagName)
{
    const std::pair<Qt::MatchFlag, QLatin1String> flagNames[] = {
        {Qt::MatchStartsWith, QLatin1String{"Qt.MatchStartsWith"}},
        {Qt::MatchRegExp, QLatin1String{"Qt.MatchRegExp"}},
        {Qt::MatchExactly, QLatin1String{"Qt.MatchExactly"}},
    };
    for (auto &&elm : flagNames) {
        if (elm.second == flagName)
            return elm.first;
    }
    qWarning("%s: unknown flag %s", Q_FUNC_INFO, qPrintable(flagName));
    return Qt::MatchStartsWith;
}

static void posToModelIndex(const QAbstractItemModel *model,
                            const QList<int> &pos, QModelIndex &mi)
{
    if (pos.isEmpty())
        return;
    auto it = pos.begin();
    int column = *it;
    ++it;
    assert(it != pos.end());
    int row = *it;
    mi = model->index(row, column);
    for (++it; it != pos.end(); ++it) {
        column = *it;
        ++it;
        assert(it != pos.end());
        row = *it;
        mi = mi.child(row, column);
    }
}

static bool canNotFind(QWidget &w)
{
    // in Qt5 we can start app find all widgets via parent<->child tree
    // but they are actually not visible on real screen
    // so check that widget visible on screen
    const QPoint pos = w.mapToGlobal(w.rect().center());
    {
        // check that such check really works
        QWidget *window = QApplication::topLevelAt(pos);
        if (window == nullptr) {
            DBGPRINT("%s: no window at such point %d %d", Q_FUNC_INFO, pos.x(),
                     pos.y());
            return false;
        }
        if (!window->testAttribute(Qt::WA_TransparentForMouseEvents)) {
            QWidget *child = window->childAt(window->mapFromGlobal(pos));
            if (child == nullptr) {
                QObject *child = &w;
                while (child->parent() != nullptr)
                    child = child->parent();
                const auto topLevelWidgets = QApplication::topLevelWidgets();
                auto it = std::find(topLevelWidgets.begin(),
                                    topLevelWidgets.end(), child);
                if (it == topLevelWidgets.end()) {
                    DBGPRINT("%s: top parent not window", Q_FUNC_INFO);
                    return false;
                }
            }
        }
    }
    QWidget *wdgAtPos = QApplication::widgetAt(pos);
    DBGPRINT("%s: g pos %d %d, w at pos %p", Q_FUNC_INFO, pos.x(), pos.y(),
             wdgAtPos);
    return wdgAtPos == nullptr;
}

static QString clickOnItemInGuiThread(qt_monkey_agent::Agent &agent,
                                      const QList<int> &idxPos,
                                      QAbstractItemView *view, bool isDblClick)
{
    DBGPRINT("%s: begin", Q_FUNC_INFO);

    QAbstractItemModel *model = view->model();
    if (model == nullptr) {
        DBGPRINT("%s: model is null\n", Q_FUNC_INFO);
        return QLatin1String(
            "ActivateItemInView failed, internal error: model is null");
    }
    QModelIndex mi;
    posToModelIndex(model, idxPos, mi);
    DBGPRINT("%s: index is %s, and %s", Q_FUNC_INFO,
             mi == QModelIndex() ? "empty" : "not empty",
             mi.isValid() ? "valid" : "not valid");

    const QRect rec = view->visualRect(mi);
    const QPoint pos = rec.center();
    QWidget *viewPort
        = view->findChild<QWidget *>(QLatin1String("qt_scrollarea_viewport"));
    DBGPRINT("%s: pos map %d %d -> %d %d", Q_FUNC_INFO, pos.x(), pos.y(),
             view->mapToGlobal(pos).x(), view->mapToGlobal(pos).y());
    moveMouseTo(agent, viewPort->mapToGlobal(pos));
    if (isDblClick) {
        DBGPRINT("%s: run dbl click on %s", Q_FUNC_INFO,
                 qPrintable(viewPort->objectName()));
        QTest::mouseClick(viewPort, Qt::LeftButton, 0, pos);
        QTest::mouseDClick(viewPort, Qt::LeftButton, 0, pos);
        DBGPRINT("%s: double click done", Q_FUNC_INFO);
    } else {
        QTest::mouseClick(viewPort, Qt::LeftButton, 0, pos);
    }
    return QString();
}

static QString activateItemInGuiThread(qt_monkey_agent::Agent &agent,
                                       QWidget *w, const QString &itemName,
                                       bool isDblClick, Qt::MatchFlag matchFlag)
{
    DBGPRINT("%s: begin: item_name %s", Q_FUNC_INFO, qPrintable(itemName));

    if (auto menu = qobject_cast<QMenu *>(w)) {
        const QList<QAction *> acts = w->actions();

        for (QAction *action : acts) {
            DBGPRINT("%s: check %s", Q_FUNC_INFO, qPrintable(action->text()));
            if (action->text() == itemName) {
                DBGPRINT("%s: found", Q_FUNC_INFO);
                QRect rect = menu->actionGeometry(action);
                QPoint pos = rect.center();
                moveMouseTo(agent, menu->mapToGlobal(pos));
                QTest::mouseClick(menu, Qt::LeftButton, 0, pos);
                return QString();
            }
        }
        DBGPRINT("%s: end: not found %s", Q_FUNC_INFO, qPrintable(itemName));
        return QStringLiteral("Item `%1' not found").arg(itemName);
    } else if (auto tw = qobject_cast<QTreeWidget *>(w)) {
        const QList<QTreeWidgetItem *> til
            = tw->findItems(itemName, matchFlag | Qt::MatchRecursive);
        if (til.isEmpty()) {
            DBGPRINT("%s: there are no such item", Q_FUNC_INFO);
            return QStringLiteral("There are no such item %1").arg(itemName);
        }
        QTreeWidgetItem *ti = til.first();
        tw->scrollToItem(ti);
        DBGPRINT("%s: item name %s, result number %d", Q_FUNC_INFO,
                 qPrintable(ti->text(0)), til.size());
        const QRect ir = tw->visualItemRect(ti);

        DBGPRINT("%s: x %d, y %d", Q_FUNC_INFO, ir.x(), ir.y());
        // tw->setCurrentItem(ti);
        // QTest::qWait(100);
        QWidget *view_port
            = tw->findChild<QWidget *>(QLatin1String("qt_scrollarea_viewport"));
        assert(view_port != nullptr);
        const QPoint pos = ir.center();
        moveMouseTo(agent, view_port->mapToGlobal(pos));
        if (isDblClick) {
            DBGPRINT("%s: run dbl click on %s", Q_FUNC_INFO,
                     qPrintable(view_port->objectName()));
            QTest::mouseClick(view_port, Qt::LeftButton, 0,
                              pos // QPoint(65, 55),
                              );
            QTest::mouseDClick(view_port, Qt::LeftButton, 0,
                               pos // QPoint(65, 55),
                               );
            DBGPRINT("%s: double click done", Q_FUNC_INFO);
        } else {
            QTest::mouseClick(view_port, Qt::LeftButton, 0,
                              pos // QPoint(65, 55),
                              );
        }

        return QString();
    } else if (auto qcb = qobject_cast<QComboBox *>(w)) {
        const int idx = qcb->findText(itemName);
        if (idx == -1) {
            DBGPRINT("%s: can not find such item %s", Q_FUNC_INFO,
                     qPrintable(itemName));
            return QStringLiteral("There are no such item %1").arg(itemName);
        }

        QList<int> pos = {0, idx};
        clickOnItemInGuiThread(agent, pos, qcb->view(), false);
        // hack to fix drop down list hiding
        QTest::keyClick(qcb->view(), Qt::Key_Enter);
        return QString();
    } else if (auto tb = qobject_cast<QTabBar *>(w)) {
        DBGPRINT("(%s, %d): this is tab bar", Q_FUNC_INFO, __LINE__);
        const int n = tb->count();
        for (int i = 0; i < n; ++i) {
            if (tb->tabText(i) == itemName) {
                DBGPRINT("(%s, %d) set current index to %d", Q_FUNC_INFO,
                         __LINE__, i);
                const QRect tabRect = tb->tabRect(i);
                if (tabRect.isNull())
                    return QStringLiteral("Null rect for tab %1").arg(itemName);
                // tb->setCurrentIndex(i);
                const QPoint clickPos = tabRect.center();
                moveMouseTo(agent, tb->mapToGlobal(clickPos));
                QTest::mouseClick(tb, Qt::LeftButton, 0, clickPos);
                return QString();
            }
        }
        DBGPRINT("%s: item %s not found", Q_FUNC_INFO, qPrintable(itemName));
        return QStringLiteral("There are no such item %1 in QTabBar")
            .arg(itemName);
    } else if (auto lw = qobject_cast<QListWidget *>(w)) {
        DBGPRINT("(%s, %d): this is list widget", Q_FUNC_INFO, __LINE__);
        const QList<QListWidgetItem *> itms
            = lw->findItems(itemName, Qt::MatchExactly);
        if (itms.isEmpty()) {
            return QStringLiteral("There are no such item %1 in QListWidget")
                .arg(itemName);
        }
        QListWidgetItem *it = itms.first();
        QRect ir = lw->visualItemRect(it);

        DBGPRINT("%s: x %d, y %d", Q_FUNC_INFO, ir.x(), ir.y());
        QWidget *viewPort
            = lw->findChild<QWidget *>(QLatin1String("qt_scrollarea_viewport"));
        assert(viewPort != nullptr);
        const QPoint pos = ir.center();
        moveMouseTo(agent, viewPort->mapToGlobal(pos));
        if (isDblClick) {
            DBGPRINT("%s: run dbl click on %s", Q_FUNC_INFO,
                     qPrintable(viewPort->objectName()));
            QTest::mouseClick(viewPort, Qt::LeftButton, 0,
                              pos // QPoint(65, 55),
                              );
            QTest::mouseDClick(viewPort, Qt::LeftButton, 0,
                               pos // QPoint(65, 55),
                               );
            DBGPRINT("%s: double click done", Q_FUNC_INFO);
        } else {
            QTest::mouseClick(viewPort, Qt::LeftButton, 0,
                              pos // QPoint(65, 55),
                              );
        }
        return QString();
    } else if (auto lv = qobject_cast<QListView *>(w)) {
        const QAbstractItemModel *m = lv->model();
        for (int i = 0; i < m->rowCount(); ++i) {
            const QString data = m->index(i, 0).data().toString();
            DBGPRINT("%s: we check\n`%s'\nvs\n`%s'\n", Q_FUNC_INFO,
                     qPrintable(data), qPrintable(itemName));
            if (itemName == data) {
                const QRect r = lv->visualRect(m->index(i, 0));
                QWidget *viewPort = lv->findChild<QWidget *>(
                    QLatin1String("qt_scrollarea_viewport"));
                assert(viewPort != nullptr);
                const QPoint pos = r.center();
                moveMouseTo(agent, viewPort->mapToGlobal(pos));
                QTest::mouseClick(viewPort, Qt::LeftButton, 0, pos);
                break;
            }
        }
        return QString();
    } else {
        DBGPRINT("%s: unknown type of widget", Q_FUNC_INFO);
        return QStringLiteral("Activate item probelm: unknown type of widget");
    }
}

} // namespace {

void qt_monkey_agent::clickInGuiThread(qt_monkey_agent::Agent &agent,
                                       const QPoint &posA, QWidget &wA,
                                       Qt::MouseButton btn, bool dblClick)
{
    DBGPRINT("%s: begin dblClick %s, x %d, y %d, %p", Q_FUNC_INFO,
             dblClick ? "true" : "false", posA.x(), posA.y(), &wA);

    QWidget *chw = wA.childAt(posA);
    QWidget *w = &wA;
    QPoint pos = posA;
    if (chw != nullptr) {
        DBGPRINT("%s: there is child at pos", Q_FUNC_INFO);
        pos = chw->mapFrom(w, pos);
        w = chw;
    }
    DBGPRINT("%s: width %d, height %d", Q_FUNC_INFO, w->width(), w->height());
    if (pos.x() >= w->width())
        pos.rx() = w->width() / 2 - 1;
    if (pos.y() >= w->height())
        pos.ry() = w->height() / 2 - 1;

    DBGPRINT("%s: class name is %s", Q_FUNC_INFO, w->metaObject()->className());
    QMenuBar *mb = qobject_cast<QMenuBar *>(w);
    if (mb != nullptr) {
        // we click on menu, to fix different size of font issue, lets
        // click on the middle
        DBGPRINT("%s: this widget is QMenuBar", Q_FUNC_INFO);
        pos.ry() = w->height() / 2 - 1;
    }

    QLineEdit *le = qobject_cast<QLineEdit *>(w);
    if (le != nullptr) {
        DBGPRINT("%s: this is line edit", Q_FUNC_INFO);
// some times click on line edit do not give
// focus to it, because of different size on different OSes
#ifdef DEBUG_SCRIPT_API
        QRect cr =
#endif
            MyLineEdit::adjustedContentsRect(*le);
        DBGPRINT("%s: begin of content of QLineEdit  x %d, y %d, w %d, h %d",
                 Q_FUNC_INFO, cr.x(), cr.y(), cr.width(), cr.height());
    }

    moveMouseTo(agent, w->mapToGlobal(pos));

    if (dblClick)
        QTest::mouseDClick(w, btn, 0, pos, -1);
    else
        QTest::mouseClick(w, btn, 0, pos, -1);
}

QWidget *qt_monkey_agent::getWidgetWithSuchName(
    qt_monkey_agent::Agent &agent, const QString &objectName,
    const int maxTimeToFindWidgetSec, bool shouldBeEnabled)
{
    DBGPRINT("%s begin, search %s", Q_FUNC_INFO, qPrintable(objectName));
    QWidget *w = nullptr;

    const int maxAttempts
        = (maxTimeToFindWidgetSec * 1000) / sleepTimeForWaitWidgetMs + 1;
    for (int i = 0; i < maxAttempts; ++i) {
        agent.runCodeInGuiThreadSync([&w, &objectName, shouldBeEnabled] {
            w = doGetWidgetWithSuchName(objectName, shouldBeEnabled);
            DBGPRINT("%s, %d: doGetWidgetWithSuchName return w '%s'",
                     Q_FUNC_INFO, __LINE__,
                     w != nullptr ? qPrintable(w->objectName()) : "nullptr");
            if (w != nullptr && canNotFind(*w)) {
                DBGPRINT("%s: canNotFind return true", Q_FUNC_INFO);
                w = nullptr;
            }
            return QString();
        });

        if (w == nullptr
            || (shouldBeEnabled && !(w->isVisible() && w->isEnabled()))) {
            DBGPRINT("%s, %d: w '%s', v %d e %d", Q_FUNC_INFO, __LINE__,
                     w != nullptr ? qPrintable(w->objectName()) : "nullptr",
                     static_cast<int>(w ? w->isVisible() : 0),
                     static_cast<int>(w ? w->isEnabled() : 0));
            w = nullptr;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(sleepTimeForWaitWidgetMs));
        } else {
            DBGPRINT("%s: widget found", Q_FUNC_INFO);
            break;
        }
    }
    return w;
}

ScriptAPI::ScriptAPI(Agent &agent, QObject *parent)
    : QObject(parent), agent_(agent)
{
}

void ScriptAPI::log(const QString &msgStr)
{
    agent_.sendToLog(std::move(msgStr));
}

void ScriptAPI::doMouseClick(const QString &widgetName,
                             const QString &buttonName, int x, int y,
                             bool doubleClick)
{
    QWidget *w = getWidgetWithSuchName(agent_, widgetName,
                                       waitWidgetAppearTimeoutSec_, true);

    if (w == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("Can not find widget with such name %1")
                .arg(widgetName));
        return;
    }
    Qt::MouseButton btn;
    if (!stringToMouseButton(buttonName, btn)) {
        agent_.throwScriptError(
            QStringLiteral("Unknown mouse button %1").arg(buttonName));
        return;
    }

    const QPoint pos{x, y};
    Agent *agent = &agent_;
    agent_.runCodeInGuiThreadSyncWithTimeout(
        [pos, doubleClick, w, btn, agent] {
            assert(w != nullptr);
            assert(agent != nullptr);
            clickInGuiThread(*agent, pos, *w, btn, doubleClick);
            return QString();
        },
        newEventLoopWaitTimeoutSecs_);
}

void ScriptAPI::doClickItem(const QString &objectName, const QString &itemName,
                            bool isDblClick, Qt::MatchFlag searchItemFlag)
{
    DBGPRINT("%s: begin object_name %s", Q_FUNC_INFO, qPrintable(objectName));

    QWidget *w = getWidgetWithSuchName(agent_, objectName,
                                       waitWidgetAppearTimeoutSec_, true);
    if (w == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("Can not find widget with such name %1")
                .arg(objectName));
        return;
    }

    if (qobject_cast<QMenu *>(w) == nullptr
        && qobject_cast<QTreeWidget *>(w) == nullptr
        && qobject_cast<QComboBox *>(w) == nullptr
        && qobject_cast<QTabBar *>(w) == nullptr
        && qobject_cast<QListWidget *>(w) == nullptr
        && qobject_cast<QListView *>(w) == nullptr) {
        agent_.throwScriptError(QStringLiteral(
            "Can not activateItem for object not: QMenu or "
            "QTreeWidget or QComboBox or QTabBar or QListWidget or "
            "QListView class"));
        return;
    }
    Agent *agent = &agent_;
    QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout(
        [w, isDblClick, itemName, searchItemFlag, agent] {
            assert(agent != nullptr);
            return activateItemInGuiThread(*agent, w, itemName, isDblClick,
                                           searchItemFlag);
        },
        newEventLoopWaitTimeoutSecs_);
    if (!errMsg.isEmpty()) {
        DBGPRINT("%s: error %s", Q_FUNC_INFO, qPrintable(errMsg));
        agent_.throwScriptError(std::move(errMsg));
    }
    DBGPRINT("%s: done", Q_FUNC_INFO);
}

void ScriptAPI::mouseClick(const QString &widgetName, const QString &button,
                           int x, int y)
{
    Step step(agent_);
    doMouseClick(widgetName, button, x, y, false);
}

void ScriptAPI::mouseDClick(const QString &widgetName, const QString &button,
                            int x, int y)
{
    Step step(agent_);
    doMouseClick(widgetName, button, x, y, true);
}

void ScriptAPI::activateItem(const QString &widget, const QString &actionName)
{
    Step step(agent_);
#ifdef Q_OS_MAC
    {
        auto ptr = agent_.menuItemsOnMac_.get();
        auto range = ptr->equal_range(widget);
        for (auto it = range.first; it != range.second; ++it)
            if (it->second != nullptr && it->second->text() == actionName) {
                auto action = it->second;
                QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout(
                    [action] {
                        action->trigger();
                        return QString();
                    },
                    newEventLoopWaitTimeoutSecs_);
                if (!errMsg.isEmpty())
                    agent_.throwScriptError(std::move(errMsg));
                return;
            }
    }
#endif
    doClickItem(widget, actionName, false);
}

void ScriptAPI::activateItem(const QString &widget, const QString &actionName,
                             const QString &searchFlags)
{
    Step step(agent_);
    doClickItem(widget, actionName, false, matchFlagFromString(searchFlags));
}

void ScriptAPI::expandItemInTree(const QString &treeWidgetName,
                                 const QString &itemName)
{
    Step step(agent_);
    QWidget *w = getWidgetWithSuchName(agent_, treeWidgetName,
                                       waitWidgetAppearTimeoutSec_, true);
    if (w == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("Can not find such widget %1").arg(treeWidgetName));
        return;
    }
    auto treeWidget = qobject_cast<QTreeWidget *>(w);
    if (treeWidget == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("%1 is not QTreeWidget").arg(treeWidgetName));
        return;
    }
    QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout(
        [itemName, treeWidget] {
            QList<QTreeWidgetItem *> til = treeWidget->findItems(
                itemName, Qt::MatchStartsWith | Qt::MatchRecursive);
            if (til.isEmpty()) {
                qWarning("%s: there are no such item", Q_FUNC_INFO);
                return QString("Item `%1' not found").arg(itemName);
            }
            QTreeWidgetItem *ti = til.first();
            treeWidget->expandItem(ti);
            return QString();
        },
        newEventLoopWaitTimeoutSecs_);

    if (!errMsg.isEmpty()) {
        DBGPRINT("%s: error %s", Q_FUNC_INFO, qPrintable(errMsg));
        agent_.throwScriptError(std::move(errMsg));
    }
}

void ScriptAPI::Wait(int ms)
{
    Step step(agent_);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

ScriptAPI::Step::Step(Agent &agent)
{
    agent.scriptCheckPoint();
    if (agent.demonstrationMode()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

ScriptAPI::Step::~Step() {}

void ScriptAPI::activateItemInView(const QString &widgetName,
                                   const QList<QVariant> &vpos)
{
    Step step(agent_);

    DBGPRINT("%s: begin widget %s", Q_FUNC_INFO, qPrintable(widgetName));

    QWidget *w = getWidgetWithSuchName(agent_, widgetName,
                                       waitWidgetAppearTimeoutSec_, true);
    if (w == nullptr) {
        DBGPRINT("%s: can not find widget", Q_FUNC_INFO);
        agent_.throwScriptError(
            QStringLiteral("Can not find widget with such name %1")
                .arg(widgetName));
        return;
    }
    auto view = qobject_cast<QTreeView *>(w);
    if (view == nullptr) {
        DBGPRINT("%s: can not (dbl)click in not QTreeView", Q_FUNC_INFO);
        agent_.throwScriptError(QStringLiteral(
            "Can not activate(double click) in not QTreeView widget"));
        return;
    }

    if (vpos.size() % 2) {
        DBGPRINT("%s: wrong position", Q_FUNC_INFO);
        agent_.throwScriptError(
            QStringLiteral("wrong position in treeview, should be even"));
        return;
    }

    QList<int> pos;

    for (const QVariant &var : vpos)
        pos.push_back(var.toInt());
    Agent *agent = &agent_;
    QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout(
        [pos, view, agent] {
            assert(agent != nullptr);
            return clickOnItemInGuiThread(*agent, pos, view, false);
        },
        newEventLoopWaitTimeoutSecs_);

    if (!errMsg.isEmpty()) {
        DBGPRINT("%s: error %s", Q_FUNC_INFO, qPrintable(errMsg));
        agent_.throwScriptError(std::move(errMsg));
    }
}

void ScriptAPI::expandItemInTreeView(const QString &treeName,
                                     const QList<QVariant> &vpos)
{
    Step step(agent_);
    QWidget *w = getWidgetWithSuchName(agent_, treeName,
                                       waitWidgetAppearTimeoutSec_, true);
    if (w == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("Can not find such widget %1").arg(treeName));
        return;
    }

    auto view = qobject_cast<QTreeView *>(w);
    if (view == nullptr) {
        DBGPRINT("%s: can not (dbl)click in not QTreeView", Q_FUNC_INFO);
        agent_.throwScriptError(QStringLiteral(
            "Can not activate(double click) in not QTreeView widget"));
        return;
    }

    if (vpos.size() % 2) {
        agent_.throwScriptError(
            QStringLiteral("wrong position in treeview(%1), should be even")
                .arg(treeName));
        return;
    }

    QList<int> pos;
    for (const QVariant &var : vpos)
        pos.push_back(var.toInt());

    QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout(
        [view, pos] {
            QAbstractItemModel *model = view->model();
            if (model == nullptr)
                return QStringLiteral(
                    "ExpandItemInTree failed, internal error: model is null");

            QModelIndex mi;
            posToModelIndex(model, pos, mi);
            view->setExpanded(mi, true);
            return QString();
        },
        newEventLoopWaitTimeoutSecs_);

    if (!errMsg.isEmpty()) {
        DBGPRINT("%s: error %s", Q_FUNC_INFO, qPrintable(errMsg));
        agent_.throwScriptError(std::move(errMsg));
    }
}

void ScriptAPI::keyClick(const QString &widgetName, const QString &keyseqStr)
{
    Step step(agent_);

    DBGPRINT("%s begin name %s, keys %s", Q_FUNC_INFO, qPrintable(widgetName),
             qPrintable(keyseqStr));

    QWidget *w = getWidgetWithSuchName(agent_, widgetName,
                                       waitWidgetAppearTimeoutSec_, true);

    if (w == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("Can not find widget with such name %1")
                .arg(widgetName));
        return;
    }

    const QKeySequence keySeq = QKeySequence::fromString(keyseqStr);
    if (keySeq.isEmpty()) {
        agent_.throwScriptError(
            QStringLiteral("Invalid key sequnce(%1): empty").arg(keyseqStr));
        return;
    }
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    for (decltype(keySeq.count()) i = 0;
         keySeq.count() > 0 && i < (keySeq.count() - 1); ++i)
        modifiers |= static_cast<Qt::KeyboardModifier>(keySeq[i]);
    QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout(
        [w, keySeq, modifiers] {
            if (!w->hasFocus())
                w->setFocus(Qt::ShortcutFocusReason);
            DBGPRINT("%s: key(%s) click for widget", Q_FUNC_INFO,
                     qPrintable(keySeq.toString()));
            QTest::keyClick(w, static_cast<Qt::Key>(keySeq[keySeq.count() - 1]),
                            modifiers, -1);
            DBGPRINT("%s: key(%s) click for widget DONE", Q_FUNC_INFO,
                     qPrintable(keySeq.toString()));
            return QString();
        },
        newEventLoopWaitTimeoutSecs_);

    if (!errMsg.isEmpty()) {
        DBGPRINT("%s: error %s", Q_FUNC_INFO, qPrintable(errMsg));
        agent_.throwScriptError(std::move(errMsg));
    }
}

void ScriptAPI::chooseWindowWithTitle(const QString &widgetName,
                                      const QString &title)
{
    Step step(agent_);
    DBGPRINT("%s: begin", Q_FUNC_INFO);
    QWidget *w = getWidgetWithSuchName(agent_, widgetName,
                                       waitWidgetAppearTimeoutSec_, true);

    if (w == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("There is no such widget %1").arg(widgetName));
        return;
    }
    auto workspace = qobject_cast<QWorkspace *>(w);
    if (workspace == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("This is not QWorkspace %1").arg(widgetName));
        return;
    }
    QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout(
        [workspace, title] {
            const auto wl = workspace->
#if QT_VERSION < 0x050000
                            windowList()
#else
                            subWindowList()
#endif
                ;
            for (auto &&win : wl) {
                if (win->windowTitle() == title) {
                    workspace->
#if QT_VERSION < 0x050000
                        setActiveWindow(win)
#else
                        setActiveSubWindow(win)
#endif
                        ;
                    return QString();
                }
            }
            return QStringLiteral("No window with such title %1").arg(title);
        },
        newEventLoopWaitTimeoutSecs_);

    if (!errMsg.isEmpty()) {
        DBGPRINT("%s: error %s", Q_FUNC_INFO, qPrintable(errMsg));
        agent_.throwScriptError(std::move(errMsg));
    }
}

void qt_monkey_agent::moveMouseTo(qt_monkey_agent::Agent &agent,
                                  const QPoint &to)
{
    DBGPRINT("%s: %d %d -> %d %d", Q_FUNC_INFO, QCursor::pos().x(),
             QCursor::pos().y(), to.x(), to.y());
    if (agent.demonstrationMode()) {
        static constexpr int timeoutPerPixelMs = 6;

        const QPoint from = QCursor::pos();
        DBGPRINT("%s: was x %d y %d", Q_FUNC_INFO, from.x(), from.y());
        const int dx = to.x() - from.x();
        const int step = dx >= 0 ? 1 : -1;
        const int dy = to.y() - from.y();
        const double k = double(dy) / dx;

        for (int x = 0; x != dx + step; x += step) {
            const int y = static_cast<int>(k * x + 0.5);
            QCursor::setPos(from.x() + x, from.y() + y);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents,
                                            timeoutPerPixelMs / 2);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(timeoutPerPixelMs / 2));
        }
    }
    DBGPRINT("%s: now x %d y %d", Q_FUNC_INFO, to.x(), to.y());
    QCursor::setPos(to);
    DBGPRINT("%s: real now x %d y %d", Q_FUNC_INFO, QCursor::pos().x(),
             QCursor::pos().y());
}

void ScriptAPI::setDemonstrationMode(bool val)
{
    Step step(agent_);
    agent_.setDemonstrationMode(val);
}

void ScriptAPI::pressButtonWithText(const QString &parentNameWidget,
                                    const QString &btnText)
{
    Step step(agent_);

    QWidget *w = getWidgetWithSuchName(agent_, parentNameWidget,
                                       waitWidgetAppearTimeoutSec_, true);
    if (w == nullptr) {
        agent_.throwScriptError(
            QStringLiteral("There is no such widget %1").arg(parentNameWidget));
        return;
    }
    const int maxAttempts
        = (waitWidgetAppearTimeoutSec_ * 1000) / sleepTimeForWaitWidgetMs + 1;
    for (int i = 0; i < maxAttempts; ++i) {
        auto agent = &agent_;
        const QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout(
            [agent, btnText, w] {
                const QObjectList &clist = w->children();
                for (QObject *qobj : clist)
                    if (auto bt = qobject_cast<QAbstractButton *>(qobj)) {
                        if (bt->text() == btnText) {
                            DBGPRINT("%s: button was found", Q_FUNC_INFO);
                            clickInGuiThread(*agent, bt->rect().center(), *bt,
                                             Qt::LeftButton, false);
                            return QString();
                        }
                    }
                return QStringLiteral("not found");
            },
            newEventLoopWaitTimeoutSecs_);

        if (errMsg.isEmpty()) {
            DBGPRINT("%s: button found", Q_FUNC_INFO);
            return;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(sleepTimeForWaitWidgetMs));
    }
    agent_.throwScriptError(
        QStringLiteral("There is no such button, parent %1, text %2")
            .arg(parentNameWidget)
            .arg(btnText));
}

void ScriptAPI::Assert(bool condition)
{
    Step step(agent_);
    if (!condition)
        agent_.throwScriptError(QStringLiteral("Assertion failed"));
}

void ScriptAPI::AssertEqual(const QString &s1, const QString &s2)
{
    Step step(agent_);
    if (s1 != s2)
        agent_.throwScriptError(
            QStringLiteral("Assertion failed: Expect \"%1\", Actual \"%2\"")
                .arg(s1)
                .arg(s2));
}

QObject *ScriptAPI::getObjectById(const QString &id)
{
    Step step(agent_);
    QWidget *w
        = getWidgetWithSuchName(agent_, id, waitWidgetAppearTimeoutSec_, false);
    if (w == nullptr)
        agent_.throwScriptError(
            QStringLiteral("There is no such widget %1").arg(id));
    return w;
}
