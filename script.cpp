#include "script.hpp"

#include <QtCore/QRegExp>
#include <QtCore/QStringList>

using qt_monkey_agent::Private::Script;

std::list<Script> Script::splitToExecutableParts(const QString &fileName,
                                                 const QString &scriptCode)
{
    std::list<Script> res;

    QRegExp rxSplitOrNewLine(
        QRegExp::escape(QLatin1String("<<<RESTART FROM HERE>>>")) + "|\n");
    int pos = 0;
    int prevPos = 0;
    int lineno = 1;
    int curLine = 1;
    while ((pos = rxSplitOrNewLine.indexIn(scriptCode, pos)) != -1) {
        if (scriptCode[pos] == '\n') {
            ++curLine;
        } else {
            res.emplace_back(fileName, lineno,
                             scriptCode.mid(prevPos, pos - prevPos));
            lineno = curLine;
            prevPos = pos + rxSplitOrNewLine.matchedLength();
        }
        pos = pos + rxSplitOrNewLine.matchedLength();
    }

    if (prevPos < scriptCode.length())
        res.emplace_back(fileName, lineno, scriptCode.mid(prevPos));
    return res;
}
