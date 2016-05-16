#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <utility>

namespace qt_monkey_agent
{
namespace Private
{
class Script final
{
public:
    Script() = default;
    Script(const Script &) = default;
    explicit Script(QString code) : code_{std::move(code)} {}
    Script(const QString &fileName, int lineNum, QString code)
        : fileName_(fileName), lineno_(lineNum), code_(std::move(code))
    {
    }
    ~Script() = default;

    const QString &code() const { return code_; }
    void releaseCode(QString &code) { code = std::move(code_); }
    static std::list<Script> splitToExecutableParts(const QString &fileName,
                                                    const QString &scriptCode);
    // start from 1
    int beginLineNum() const { return lineno_; }
    const QString &fileName() const { return fileName_; }
    bool runAfterAppStart() const { return runAfterStart_; }
    void setRunAfterAppStart(bool val) { runAfterStart_ = val; }
private:
    QString fileName_;
    int lineno_ = 1;
    QString code_;
    bool runAfterStart_ = false;
};
}
}

Q_DECLARE_METATYPE(qt_monkey_agent::Private::Script);
