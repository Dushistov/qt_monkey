#define DEBUG_SCRIPT_API
#include "script_api.hpp"

#include <QApplication>
#include <QWidget>
#include <QMenuBar>
#include <QLineEdit>
#include <QStyleOption>
#include <QtTest/QTest>
#include <QtCore/QStringList>
#include <QtScript/QScriptEngine>
#include <cassert>
#include <thread>
#include <chrono>

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

namespace {
class MyLineEdit final : public QLineEdit {
public:
	static QRect adjustedContentsRect(const QLineEdit& le);
};

QRect MyLineEdit::adjustedContentsRect(const QLineEdit& le) 
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
}

QWidget *getWidgetWithSuchName(qt_monkey_agent::Agent &agent, const QString &objectName,
                               const int maxTimeToFindWidgetSec,
                               bool shouldBeEnabled)
{
    DBGPRINT("%s begin, search %s", Q_FUNC_INFO, qPrintable(objectName));
    QWidget *w = nullptr;
    static const int sleepTimeForWaitWidgetMs = 70;

    const int maxAttempts
        = (maxTimeToFindWidgetSec * 1000) / sleepTimeForWaitWidgetMs + 1;
    for (int i = 0; i < maxAttempts; ++i) {
        agent.runCodeInGuiThreadSync([&w, &objectName] { w = doGetWidgetWithSuchName(objectName); });

        if (w == nullptr || (shouldBeEnabled && !(w->isVisible() && w->isEnabled()))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeForWaitWidgetMs));
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

void ScriptAPI::log(QString msgStr) { agent_.sendToLog(std::move(msgStr)); }

void ScriptAPI::moveMouseTo(const QPoint &point)
{
    QCursor::setPos(point);
}

void ScriptAPI::clickInGuiThread(const QPoint &posA, QWidget &wA, Qt::MouseButton btn, bool dblClick)
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
		//we click on menu, to fix different size of font issue, lets
		//click on the middle 
		DBGPRINT("%s: this widget is QMenuBar", Q_FUNC_INFO);
		pos.ry() = w->height() / 2 - 1;
	}

	QLineEdit *le = qobject_cast<QLineEdit *>(w);
	if (le != nullptr) {
		DBGPRINT("%s: this is line edit", Q_FUNC_INFO);
		//some times click on line edit do not give 
		//focus to it, because of different size on different OSes
		QRect cr = MyLineEdit::adjustedContentsRect(*le);
		DBGPRINT("%s: begin of content of QLineEdit  x %d, y %d, w %d, h %d",
		       Q_FUNC_INFO, cr.x(), cr.y(), cr.width(), cr.height());
	}

	moveMouseTo(w->mapToGlobal(pos));

	if (dblClick)
		QTest::mouseDClick(w,
				   btn,
				   0,
				   pos,
				   -1);
	else
		QTest::mouseClick(w,
				  btn,
				  0,
				  pos,
				  -1);
}

void ScriptAPI::doMouseClick(const QString &widgetName, const QString &buttonName, int x, int y, bool doubleClick)
{
   QWidget *w = getWidgetWithSuchName(agent_, widgetName, waitWidgetAppearTimeoutSec_, true);
    
    if (w == nullptr) {
        agent_.throwScriptError(
                    QStringLiteral("Can not find widget with such name %1")
                    .arg(widgetName));
        return;
    }
   Qt::MouseButton btn;
   if (!stringToMouseButton(buttonName, btn)) {
       agent_.throwScriptError(
           QStringLiteral("Unknown mouse button %1")
           .arg(buttonName));
       return;
   }

   const QPoint pos{x, y};
   agent_.runCodeInGuiThreadSyncWithTimeout([&pos, doubleClick, w, this, btn]{
           assert(w != nullptr);
           clickInGuiThread(pos, *w, btn, doubleClick);
       }, newEventLoopWaitTimeoutSecs_);
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
