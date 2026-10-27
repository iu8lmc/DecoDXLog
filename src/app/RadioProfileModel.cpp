#include "app/RadioProfileModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

namespace decolog::app {

namespace {
constexpr auto kProfilesKey = "rig/profiles";
constexpr auto kActiveKey = "rig/activeProfile";
}

RadioProfileModel::RadioProfileModel(QObject* parent)
    : QAbstractListModel(parent)
{
    load();
}

int RadioProfileModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_profiles.size();
}

QVariant RadioProfileModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_profiles.size())
        return {};
    const Profile& profile = m_profiles.at(index.row());
    if (role == IdRole)
        return profile.id;
    if (role == NameRole || role == Qt::DisplayRole)
        return profile.name;
    return {};
}

QHash<int, QByteArray> RadioProfileModel::roleNames() const
{
    return {{IdRole, "profileId"}, {NameRole, "name"}};
}

QString RadioProfileModel::activeName() const
{
    return byId(m_activeId).value(QStringLiteral("name")).toString();
}

QVariantMap RadioProfileModel::asMap(const Profile& profile) const
{
    QVariantMap out = profile.values;
    out.insert(QStringLiteral("id"), profile.id);
    out.insert(QStringLiteral("name"), profile.name);
    return out;
}

QVariantMap RadioProfileModel::get(int row) const
{
    return row >= 0 && row < m_profiles.size() ? asMap(m_profiles.at(row)) : QVariantMap{};
}

QVariantMap RadioProfileModel::byId(const QString& id) const
{
    for (const Profile& profile : m_profiles) {
        if (profile.id == id)
            return asMap(profile);
    }
    return {};
}

int RadioProfileModel::rowForId(const QString& id) const
{
    for (qsizetype i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == id)
            return static_cast<int>(i);
    }
    return -1;
}

void RadioProfileModel::load()
{
    m_profiles.clear();
    const QSettings settings;
    const QByteArray raw = settings.value(QString::fromLatin1(kProfilesKey)).toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (document.isArray()) {
        for (const QJsonValue& value : document.array()) {
            const QJsonObject object = value.toObject();
            const QString id = object.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty())
                continue;
            Profile profile;
            profile.id = id;
            profile.name = object.value(QStringLiteral("name")).toString().trimmed();
            if (profile.name.isEmpty())
                profile.name = QStringLiteral("Radio %1").arg(m_profiles.size() + 1);
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (it.key() != QLatin1String("id") && it.key() != QLatin1String("name"))
                    profile.values.insert(it.key(), it.value().toVariant());
            }
            m_profiles.append(profile);
        }
    }
    m_activeId = settings.value(QString::fromLatin1(kActiveKey)).toString().trimmed();
    ensureActive();
}

void RadioProfileModel::save() const
{
    QJsonArray array;
    for (const Profile& profile : m_profiles) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), profile.id);
        object.insert(QStringLiteral("name"), profile.name);
        for (auto it = profile.values.cbegin(); it != profile.values.cend(); ++it)
            object.insert(it.key(), QJsonValue::fromVariant(it.value()));
        array.append(object);
    }
    QSettings settings;
    settings.setValue(QString::fromLatin1(kProfilesKey), QJsonDocument(array).toJson(QJsonDocument::Compact));
    settings.setValue(QString::fromLatin1(kActiveKey), m_activeId);
}

void RadioProfileModel::ensureActive()
{
    QString chosen;
    if (rowForId(m_activeId) >= 0)
        chosen = m_activeId;
    else if (!m_profiles.isEmpty())
        chosen = m_profiles.constFirst().id;
    const bool changed = chosen != m_activeId;
    m_activeId = chosen;
    if (changed)
        save();
}

QString RadioProfileModel::create(const QString& name, const QVariantMap& values, bool makeActive)
{
    Profile profile;
    profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile.name = name.trimmed().isEmpty() ? QStringLiteral("Radio %1").arg(m_profiles.size() + 1) : name.trimmed();
    profile.values = values;
    profile.values.remove(QStringLiteral("id"));
    profile.values.remove(QStringLiteral("name"));
    const int row = m_profiles.size();
    beginInsertRows({}, row, row);
    m_profiles.append(profile);
    endInsertRows();
    if (makeActive)
        m_activeId = profile.id;
    save();
    emit profilesChanged();
    emit activeChanged();
    return profile.id;
}

bool RadioProfileModel::update(const QString& id, const QVariantMap& values)
{
    const int row = rowForId(id);
    if (row < 0)
        return false;
    Profile& profile = m_profiles[row];
    const QString name = values.value(QStringLiteral("name"), profile.name).toString().trimmed();
    profile.name = name.isEmpty() ? profile.name : name;
    profile.values = values;
    profile.values.remove(QStringLiteral("id"));
    profile.values.remove(QStringLiteral("name"));
    emit dataChanged(index(row), index(row), {IdRole, NameRole, Qt::DisplayRole});
    save();
    emit profilesChanged();
    if (id == m_activeId)
        emit activeChanged();
    return true;
}

bool RadioProfileModel::remove(const QString& id)
{
    const int row = rowForId(id);
    if (row < 0 || m_profiles.size() <= 1)
        return false;
    beginRemoveRows({}, row, row);
    m_profiles.removeAt(row);
    endRemoveRows();
    if (id == m_activeId)
        m_activeId = m_profiles.constFirst().id;
    save();
    emit profilesChanged();
    emit activeChanged();
    return true;
}

bool RadioProfileModel::setActiveId(const QString& id)
{
    if (rowForId(id) < 0 || id == m_activeId)
        return rowForId(id) >= 0;
    m_activeId = id;
    save();
    emit activeChanged();
    return true;
}

} // namespace decolog::app
