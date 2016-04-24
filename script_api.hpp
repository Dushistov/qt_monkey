#pragma once

namespace qt_monkey {
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
        explicit ScriptAPI(QObject *parent = nullptr);
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
    };
}
