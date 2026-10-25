#pragma once

#include <QHash>
#include <QVariant>

namespace decolog::core {

// Independent of contest CallHistory: a call can have several memberships.
struct CallListData {
    QHash<QString, QVariantList> calls;
    QVariantMap preview;
    QString error;
    int records{0};
    int skipped{0};
};

class CallList {
public:
    static constexpr qint64 maxBytes = 32 * 1024 * 1024;
    static CallListData parse(const QByteArray& bytes, const QVariantMap& options = {}, bool preview = false);
    static QString normalizedCall(const QString& value);
};

} // namespace decolog::core
