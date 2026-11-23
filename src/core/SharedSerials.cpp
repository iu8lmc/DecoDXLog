#include "core/SharedSerials.h"

#include <QtGlobal>

namespace decolog::core {

void SharedSerials::note(int serial)
{
    if (serial > m_max)
        m_max = serial;
}

bool SharedSerials::noteRemote(int serial)
{
    const bool collision = serial > 0 && m_issued.contains(serial);
    if (collision)
        m_collision = true;
    note(serial);
    return collision;
}

int SharedSerials::serve()
{
    const int n = qMax(m_next, m_max + 1);
    m_next = n + 1;
    note(n);
    m_issued.insert(n);
    return n;
}

bool SharedSerials::isServer(const QString& me, const QStringList& others)
{
    for (const QString& id : others) {
        if (id < me)
            return false;
    }
    return true;
}

int SharedSerials::take(int localNext, bool server, bool online)
{
    if (online && server) {
        // Advance the allocator before serving: the returned value must be the
        // value actually recorded as issued, including a higher local counter.
        note(localNext - 1);
        return serve();
    }
    int n = m_reserved > 0 ? m_reserved : qMax(localNext, m_max + 1);
    m_reserved = 0;
    note(n);
    m_issued.insert(n);
    return n;
}

void SharedSerials::reserve(int serial)
{
    note(serial);
    m_reserved = serial;
}

} // namespace decolog::core
