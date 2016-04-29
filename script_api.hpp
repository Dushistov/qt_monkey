#pragma once

#include <QtCore/QObject>
#include <QtScript/QScriptable>

namespace qt_monkey_agent {

    class Agent;

    /**
     * public slots of this class are functions
     * that exposed to qt monkey script
     */
    class ScriptAPI
#ifndef Q_MOC_RUN
    final
#endif
        : public QObject, private QScriptable {
        Q_OBJECT
    public:
        explicit ScriptAPI(Agent &agent, QObject *parent = nullptr);
    public slots:
		//@{
		/**
         * Emulate click or double click on widget
		 * @param widget name of widget
		 * @param button mouse button
		 * @param x x of click in widget coordinate system
		 * @param y y of click in widget coordinate system
		 */
		void mouseClick(QString widget, QString button, int x, int y);
		void mouseDClick(QString widget, QString button, int x, int y);
		//@}
		/**
		 * send message to log
		 * @param msgStr string with message
		 */
		void log(QString msgStr);
    private:
        Agent &agent_;

        void step();
    };
}
