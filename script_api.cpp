//#define DEBUG_SCRIPT_API
#include "script_api.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QStyleOption>
#include <QTreeWidget>
#include <QWidget>
#include <QtCore/QStringList>
#include <QtScript/QScriptEngine>
#include <QtTest/QTest>
#include <cassert>
#include <chrono>
#include <thread>

#include "agent.hpp"
#include "common.hpp"
#include "script_runner.hpp"
#include "user_events_analyzer.hpp"

using qt_monkey_agent::ScriptAPI;

#ifdef DEBUG_SCRIPT_API
#define DBGPRINT(fmt, ...) qDebug(fmt, __VA_ARGS__)
#else
#define DBGPRINT(fmt, ...)                                                     \
    do {                                                                       \
    } while (false)
#endif

namespace
{
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
                                       const QString &className)
{
    for (QWidget *widget : QApplication::allWidgets()) {
        if (!className.isEmpty()
            && className == widget->metaObject()->className()) {
            DBGPRINT("%s: found widget with class %s, w %s", Q_FUNC_INFO,
                     qPrintable(className),
                     widget == nullptr ? "null" : "not null");
            return widget;
        }
        if (widget->objectName() == objectName) {
            DBGPRINT("%s: found widget %s", Q_FUNC_INFO,
                     qPrintable(objectName));
            return widget;
        }
    }

    DBGPRINT("(%s, %d): brute force do not give any suitable result",
             Q_FUNC_INFO, __LINE__);
    return nullptr;
}

static QWidget *doGetWidgetWithSuchName(const QString &objectName)
{
    QStringList names = objectName.split('.');

    if (names.isEmpty()) {
        DBGPRINT("%s: list of widget's name empty\n", Q_FUNC_INFO);
        return nullptr;
    }

    QWidget *win = qApp->activeModalWidget();
    if (win)
        DBGPRINT("(%s, %d): Modal Window %s", Q_FUNC_INFO, __LINE__,
                 qPrintable(win->objectName()));
    else
        DBGPRINT("(%s, %d): Modal Window nullptr", Q_FUNC_INFO, __LINE__);

    win = qApp->activePopupWidget();
    if (win)
        DBGPRINT("(%s, %d): popup Window %s", Q_FUNC_INFO, __LINE__,
                 qPrintable(win->objectName()));
    else
        DBGPRINT("(%s, %d): popup Window nullptr", Q_FUNC_INFO, __LINE__);

    win = qApp->activeWindow();
    if (win)
        DBGPRINT("(%s, %d): active Window %s", Q_FUNC_INFO, __LINE__,
                 qPrintable(win->objectName()));
    else
        DBGPRINT("(%s, %d): active Window nullptr", Q_FUNC_INFO, __LINE__);

    const QString &mainWidgetName = names.first();
    DBGPRINT("(%s, %d): search widget with such name %s", Q_FUNC_INFO, __LINE__,
             qPrintable(mainWidgetName));
    QList<QObject *> lst = qFindChildren<QObject *>(
        QCoreApplication::instance(), mainWidgetName);
    if (lst.isEmpty()) {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (mainWidgetName == widget->objectName()) {
                lst << widget;
                break;
            } else {
                lst = qFindChildren<QObject *>(widget, mainWidgetName);
            }

            if (lst.count())
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
        QWidget *w = bruteForceWidgetSearch(mainWidgetName, className);
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
                if (order++ != class_order)
                    continue;
                w = qobject_cast<QWidget *>(*it);
                names.removeFirst();
                break;
            }
        }

        if (it == clist.end()) {
            DBGPRINT("(%s, %d): Can not find object with such name %s, try "
                     "brute search",
                     Q_FUNC_INFO, __LINE__, qPrintable(el));
            w = bruteForceWidgetSearch(el, class_name);
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
} // namespace {

QWidget *getWidgetWithSuchName(qt_monkey_agent::Agent &agent,
                               const QString &objectName,
                               const int maxTimeToFindWidgetSec,
                               bool shouldBeEnabled)
{
    DBGPRINT("%s begin, search %s", Q_FUNC_INFO, qPrintable(objectName));
    QWidget *w = nullptr;
    static const int sleepTimeForWaitWidgetMs = 70;

    const int maxAttempts
        = (maxTimeToFindWidgetSec * 1000) / sleepTimeForWaitWidgetMs + 1;
    for (int i = 0; i < maxAttempts; ++i) {
        agent.runCodeInGuiThreadSync(
            [&w, &objectName] { w = doGetWidgetWithSuchName(objectName); return QString(); });

        if (w == nullptr
            || (shouldBeEnabled && !(w->isVisible() && w->isEnabled()))) {
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

void ScriptAPI::moveMouseTo(const QPoint &point) { QCursor::setPos(point); }

void ScriptAPI::clickInGuiThread(const QPoint &posA, QWidget &wA,
                                 Qt::MouseButton btn, bool dblClick)
{
    DBGPRINT("%s: begin dbl_click %s, x %d, y %d, %p", Q_FUNC_INFO,
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

    moveMouseTo(w->mapToGlobal(pos));

    if (dblClick)
        QTest::mouseDClick(w, btn, 0, pos, -1);
    else
        QTest::mouseClick(w, btn, 0, pos, -1);
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
    agent_.runCodeInGuiThreadSyncWithTimeout(
        [&pos, doubleClick, w, this, btn] {
            assert(w != nullptr);
            clickInGuiThread(pos, *w, btn, doubleClick);
            return QString();
        },
        newEventLoopWaitTimeoutSecs_);
}

void ScriptAPI::doClickItem(const QString &objectName, const QString &itemName,
                            bool isDblClick, Qt::MatchFlag searchItemFlag)
{
    DBGPRINT("%s: begin object_name %s", Q_FUNC_INFO, qPrintable(object_name));

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
        agent_.throwScriptError("Can not activateItem for object not: QMenu or "
                                "QTreeWidget or QComboBox or QListWidget or "
                                "QListView class");
        return;
    }
    QString errMsg = agent_.runCodeInGuiThreadSyncWithTimeout([w, isDblClick, &itemName, searchItemFlag, this] {
            return activateItemInGuiThread(w, itemName, isDblClick, searchItemFlag);
        }, newEventLoopWaitTimeoutSecs_);
    if (!errMsg.isEmpty()) {
        DBGPRINT("%s: error %s", Q_FUNC_INFO, qPrintable(errMsg));
        agent_.throwScriptError(std::move(errMsg));
    }
    DBGPRINT("%s: done", Q_FUNC_INFO);
}

void ScriptAPI::mouseClick(QString widgetName, QString button, int x, int y)
{
    agent_.scriptCheckPoint();
    doMouseClick(widgetName, button, x, y, false);
}

void ScriptAPI::mouseDClick(QString widgetName, QString button, int x, int y)
{
    agent_.scriptCheckPoint();
    doMouseClick(widgetName, button, x, y, true);
}

void ScriptAPI::activateItem(const QString &widget, const QString &actionName)
{
    agent_.scriptCheckPoint();
    doClickItem(widget, actionName, false);
}

void ScriptAPI::activateItem(const QString &widget, const QString &actionName,
                             const QString &searchFlags)
{
    agent_.scriptCheckPoint();
    doClickItem(widget, actionName, false, matchFlagFromString(searchFlags));
}

QString ScriptAPI::clickOnItemInGuiThread(const QList<int> &idxPos,
                                       QAbstractItemView *view, bool isDblClick)
{
    DBGPRINT("%s: begin", Q_FUNC_INFO);

    QAbstractItemModel *model = view->model();
    if (model == nullptr) {
        DBGPRINT("%s: model is null\n", Q_FUNC_INFO);       
       return QLatin1String("ActivateItemInView failed, internal error: model is null");
    }
    QModelIndex mi;
    posToModelIndex(model, idxPos, mi);
    DBGPRINT("%s: index is %s, and %s", Q_FUNC_INFO,
             mi == QModelIndex() ? "empty" : "not empty",
             mi.isValid() ? "valid" : "not valid");

    const QRect rec = view->visualRect(mi);
    const QPoint pos = rec.center();
    QWidget *view_port = qFindChild<QWidget *>(view, "qt_scrollarea_viewport");
    moveMouseTo(view_port->mapToGlobal(pos));
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
}

QString ScriptAPI::activateItemInGuiThread(QWidget *w, const QString &itemName,
                                           bool isDblClick,
                                           Qt::MatchFlag matchFlag)
{
    DBGPRINT("%s: begin: item_name %s", Q_FUNC_INFO, qPrintable(itemName));


    if (QMenu *menu = qobject_cast<QMenu *>(w)) {
        QList<QAction *> acts = w->actions();

        for (QAction *action : acts) {
            DBGPRINT("%s: check %s", Q_FUNC_INFO, qPrintable(action->text()));
            if (action->text() == itemName) {
                DBGPRINT("%s: found", Q_FUNC_INFO);
                QRect rect = menu->actionGeometry(action);
                QPoint pos = rect.center();
                moveMouseTo(menu->mapToGlobal(pos));
                QTest::mouseClick(menu, Qt::LeftButton, 0, pos);
                //				action->trigger();
                return QString();
            }
        }
        DBGPRINT("%s: end: not found %s", Q_FUNC_INFO, qPrintable(item_name));
        return QString("Item `%1' not found").arg(itemName);
    } else if (QTreeWidget *tw = qobject_cast<QTreeWidget *>(w)) {
        QList<QTreeWidgetItem *> til
            = tw->findItems(itemName, matchFlag | Qt::MatchRecursive);
        if (til.isEmpty()) {
            DBGPRINT("%s: there are no such item", Q_FUNC_INFO);
            return QString("There are no such item %1").arg(itemName);
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
            = qFindChild<QWidget *>(tw, "qt_scrollarea_viewport");
        assert(view_port != nullptr);
        const QPoint pos = ir.center();
        moveMouseTo(view_port->mapToGlobal(pos));
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
    } else if (QComboBox *qcb = qobject_cast<QComboBox *>(w)) {
        const int idx = qcb->findText(itemName);
        if (idx == -1) {
            DBGPRINT("%s: can not find such item %s", Q_FUNC_INFO,
                     qPrintable(item_name));
            return QString("There are no such item %1").arg(itemName);
        }

        QList<int> pos;
        pos.push_back(0);
        pos.push_back(idx);
        clickOnItemInGuiThread(pos, qcb->view(), false);
        QTest::keyClick(qcb->view(), Qt::Key_Enter);
        return QString();
    } else if (QTabBar *tb = qobject_cast<QTabBar *>(w)) {
        DBGPRINT("(%s, %d): this is tab bar", Q_FUNC_INFO, __LINE__);
        const int n = tb->count();
        moveMouseTo(tb->mapToGlobal(tb->rect().center()));
        for (int i = 0; i < n; ++i) {
            if (tb->tabText(i) == itemName) {
                DBGPRINT("(%s, %d) set current index to %d", Q_FUNC_INFO,
                         __LINE__, i);
                tb->setCurrentIndex(i);
                return QString();
            }
        }
        DBGPRINT("%s: item %s not found", Q_FUNC_INFO, qPrintable(item_name));
        return QString("There are no such item %1 in QTabBar").arg(itemName);
    } else if (QListWidget *lw = qobject_cast<QListWidget *>(w)) {
        DBGPRINT("(%s, %d): this is list widget", Q_FUNC_INFO, __LINE__);
        QList<QListWidgetItem *> itms
            = lw->findItems(itemName, Qt::MatchExactly);
        if (itms.isEmpty()) {
            return QString("There are no such item %1 in QListWidget")
                    .arg(itemName);
        }
        QListWidgetItem *it = itms.first();
        QRect ir = lw->visualItemRect(it);

        DBGPRINT("%s: x %d, y %d", Q_FUNC_INFO, ir.x(), ir.y());
        QWidget *view_port
            = qFindChild<QWidget *>(lw, "qt_scrollarea_viewport");
        assert(view_port != nullptr);
        const QPoint pos = ir.center();
        moveMouseTo(view_port->mapToGlobal(pos));
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
    } else if (QListView *lv = qobject_cast<QListView *>(w)) {
        const QAbstractItemModel *m = lv->model();
        for (int i = 0; i < m->rowCount(); ++i) {
            const QString data = m->index(i, 0).data().toString();
            DBGPRINT("%s: we check\n`%s'\nvs\n`%s'\n", Q_FUNC_INFO,
                     qPrintable(data), qPrintable(item_name));
            if (itemName == data) {
                const QRect r = lv->visualRect(m->index(i, 0));
                QWidget *view_port
                    = qFindChild<QWidget *>(lv, "qt_scrollarea_viewport");
                assert(view_port != nullptr);
                const QPoint pos = r.center();
                moveMouseTo(view_port->mapToGlobal(pos));
                QTest::mouseClick(view_port, Qt::LeftButton, 0, pos);
                break;
            }
        }
        return QString();
    } else {
        DBGPRINT("%s: unknown type of widget", Q_FUNC_INFO);
        return QLatin1String("ActivateWidget probelm: unknown type of widget");
    }
}
