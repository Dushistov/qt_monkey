#pragma once

#include <utility>
#include <QtCore/QString>
#include <QtCore/QMetaType>

namespace qt_monkey_agent
{
namespace Private
{
class Script final
{
public:
    Script() = default;
    ~Script() = default;
    Script(const Script&) = default;
    explicit Script(QString code): code_{std::move(code)} {}
    const QString &code() const { return code_; }
    void releaseCode(QString &code) { code = std::move(code_); }
private:
    QString code_;
};
}
}

Q_DECLARE_METATYPE(qt_monkey_agent::Private::Script);
