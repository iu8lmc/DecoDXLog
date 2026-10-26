// DecoDXLog — profili persistenti per la configurazione CAT delle radio.
#pragma once

#include <QAbstractListModel>
#include <QVariantMap>

namespace decolog::app {

class RadioProfileModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChanged)
    Q_PROPERTY(QString activeName READ activeName NOTIFY activeChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole };

    explicit RadioProfileModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeId() const { return m_activeId; }
    QString activeName() const;
    int count() const { return m_profiles.size(); }

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap byId(const QString& id) const;
    Q_INVOKABLE int rowForId(const QString& id) const;

    // Crea o aggiorna un profilo. Il contenuto tecnico viene fornito dal
    // RigController, così il modello resta indipendente dal CAT vero.
    QString create(const QString& name, const QVariantMap& values, bool makeActive = true);
    bool update(const QString& id, const QVariantMap& values);
    bool remove(const QString& id);
    bool setActiveId(const QString& id);

signals:
    void activeChanged();
    void profilesChanged();

private:
    struct Profile {
        QString id;
        QString name;
        QVariantMap values;
    };

    void load();
    void save() const;
    QVariantMap asMap(const Profile& profile) const;
    void ensureActive();

    QList<Profile> m_profiles;
    QString m_activeId;
};

} // namespace decolog::app
