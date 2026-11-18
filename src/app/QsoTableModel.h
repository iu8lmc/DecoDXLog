// DecoDXLog — la tabella del log, per il TableView QML.
//
// In memoria ci sono solo gli id delle righe, nell'ordine della tabella: filtri
// e ordine li fa SQLite. I valori si leggono a pagine di duecento righe quando
// la tabella li chiede, e se ne tengono poche decine: un log da un milione di
// QSO si apre in un secondo e occupa qualche megabyte, non un gigabyte.
#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QThreadPool>

#include <array>
#include <atomic>
#include <memory>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class QSqlQuery;
namespace decolog::core { class LogDatabase; }

namespace decolog::app {

class QsoTableModel : public QAbstractTableModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filtersChanged)
    Q_PROPERTY(QStringList bandFilter READ bandFilter WRITE setBandFilter NOTIFY filtersChanged)
    Q_PROPERTY(QStringList modeFilter READ modeFilter WRITE setModeFilter NOTIFY filtersChanged)
    // "yyyy-MM", vuoto = tutti i mesi.
    Q_PROPERTY(QString monthFilter READ monthFilter WRITE setMonthFilter NOTIFY filtersChanged)
    // Numero DXCC, 0 = tutte le entita'.
    Q_PROPERTY(int dxccFilter READ dxccFilter WRITE setDxccFilter NOTIFY filtersChanged)
    // "", "confirmed" (qualunque servizio), "lotw", "card", "eqsl", "unconfirmed".
    Q_PROPERTY(QString qslFilter READ qslFilter WRITE setQslFilter NOTIFY filtersChanged)
    // Id del profilo stazione, 0 = tutti.
    Q_PROPERTY(int profileFilter READ profileFilter WRITE setProfileFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString tagFilter READ tagFilter WRITE setTagFilter NOTIFY filtersChanged)
    // "yyyy-MM-dd", estremi compresi; vuoti = senza limite.
    Q_PROPERTY(QString dateFrom READ dateFrom WRITE setDateFrom NOTIFY filtersChanged)
    Q_PROPERTY(QString dateTo READ dateTo WRITE setDateTo NOTIFY filtersChanged)
    // Un campo qualsiasi con il suo valore, come nelle "Ricerche" di altri log:
    // zona CQ, zona ITU, continente, prefisso WPX, QTH, riferimenti, manager,
    // propagazione, mese della cartolina... {chiave: valore}, tutti insieme.
    Q_PROPERTY(QVariantMap fieldFilters READ fieldFilters NOTIFY filtersChanged)
    Q_PROPERTY(int columns READ columns NOTIFY layoutChanged)
    // Le colonne mostrate, nell'ordine in cui si vedono: chiavi del catalogo
    // (vedi availableColumns) o "x:CAMPO" per un campo ADIF qualsiasi.
    Q_PROPERTY(QStringList columnLayout READ columnLayout WRITE setColumnLayout NOTIFY layoutChanged)
    Q_PROPERTY(bool filtered READ filtered NOTIFY filtersChanged)
    // L'ordine delle righe: la colonna (chiave) e il verso. Di serie l'ora del
    // QSO, dal piu' recente. Un clic sull'intestazione lo cambia.
    Q_PROPERTY(QString sortKey READ sortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged)
    // Su un log grande un ordine o un filtro si preparano su un altro filo:
    // intanto la tabella resta com'era, e questo e' vero.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    // I valori che ogni riga tiene sempre, qualunque colonna si veda: servono
    // ai menu (nominativo, DXCC, etichette) anche quando la colonna e' nascosta.
    enum Column { Utc, Call, Band, Freq, Mode, RstSent, RstRcvd, Grid, Name, Comment, Qth, Country,
                  State, County, Cqz, Ituz, Iota, Dxcc, Qsl, Source, Tags, ColumnCount };
    enum Roles { IdRole = Qt::UserRole + 1, ColumnKeyRole, IsNewRole, ModeRole, CategoryRole, QslStateRole };

    // Lo stato della conferma di un QSO, per colorare la riga, dal meno al
    // piu' importante: niente, la cartolina partita e non tornata, confermato
    // solo da eQSL o QRZ (che per il DXCC non valgono), dalla cartolina, da
    // LoTW (con o senza cartolina: LoTW ha la precedenza).
    enum QslState : quint8 { QslNone = 0, QslCardSent = 1, QslOtherConfirmed = 2, QslCardConfirmed = 3,
                             QslLotwConfirmed = 4 };
    static quint8 qslStateFrom(const QString& summary);

    // Che cosa ha portato un QSO quando e' stato fatto, come Decodium 4 lo
    // dice dei decode: il primo con quell'entita' (in assoluto o sulla
    // banda), con quel continente, zona CQ, zona ITU, locatore, nominativo;
    // oppure confermato (LoTW, cartolina, eQSL), oppure un gia' lavorato. Il
    // nome e' la chiave del colore (colorNewDxcc…).
    static QStringList categoryKeys();
    // Le categorie che hanno un colore acceso: la riga prende la prima di
    // queste che le spetta. Senza questa scelta valgono tutte (le prove).
    void setActiveCategories(const QStringList& keys);
    // Quella che spetta a una riga: `firsts` sono le cose nuove che il QSO ha
    // portato (un bit per ciascuna delle prime dodici categorie), `qsl` la
    // conferma.
    QString categoryFor(quint16 firsts, quint8 qsl) const;

    // Il risultato della conta delle categorie di tutto il log.
    struct CategoryPass {
        QHash<qint64, quint16> category;
        QSet<quint64> seen[12];
        QString lastOn;
        qint64 lastId{0};
        bool valid{false};
    };

    explicit QsoTableModel(decolog::core::LogDatabase* db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_ids.size()); }
    int totalCount() const { return m_total; }
    QString filterText() const { return m_filter; }
    void setFilterText(const QString& text);
    QStringList bandFilter() const { return m_bands; }
    void setBandFilter(const QStringList& bands);
    QStringList modeFilter() const { return m_modes; }
    void setModeFilter(const QStringList& modes);
    QString monthFilter() const { return m_month; }
    void setMonthFilter(const QString& month);
    int dxccFilter() const { return m_dxcc; }
    void setDxccFilter(int dxcc);
    QString qslFilter() const { return m_qsl; }
    void setQslFilter(const QString& qsl);
    int profileFilter() const { return m_profile; }
    void setProfileFilter(int profileId);
    QString tagFilter() const { return m_tag; }
    void setTagFilter(const QString& tag);
    QString dateFrom() const { return m_dateFrom; }
    void setDateFrom(const QString& date);
    QString dateTo() const { return m_dateTo; }
    void setDateTo(const QString& date);
    QVariantMap fieldFilters() const { return m_fields; }
    // Un valore vuoto toglie il filtro su quel campo.
    Q_INVOKABLE void setFieldFilter(const QString& key, const QString& value);
    // I campi che si possono cercare cosi': [{key, label}].
    Q_INVOKABLE QVariantList fieldFilterChoices() const;
    Q_INVOKABLE QString fieldFilterLabel(const QString& key) const;
    // Come si legge un valore di quel campo (i mesi "202609" come "09/2026").
    Q_INVOKABLE QString fieldValueLabel(const QString& key, const QString& value) const;
    // I valori di quel campo nel log, ciascuno coi suoi QSO: arrivano con
    // fieldValuesReady, contati su un altro filo se il log e' in un file.
    Q_INVOKABLE void requestFieldValues(const QString& key);
    // La stessa conta, subito (per le prove e per chi ne ha bisogno ora).
    QVariantList fieldValues(const QString& key) const;
    // Gli stessi filtri su piu' log (file), in sola lettura e su un altro filo:
    // `logs` e' [{name, path}]. Il profilo stazione non conta (i numeri dei
    // profili cambiano da un log all'altro). Il risultato arriva con logsSearched.
    static constexpr int kSearchLimit = 5000;
    Q_INVOKABLE void searchLogs(const QVariantList& logs);
    // Lo stesso, subito (per le prove).
    void searchLogsNow(const QVariantList& logs, QVariantList* rows, QVariantList* perLog) const;
    int columns() const { return static_cast<int>(m_layout.size()); }
    QStringList columnLayout() const { return m_layout; }
    void setColumnLayout(const QStringList& keys);
    // Le colonne di sempre, nell'ordine di sempre.
    static QStringList defaultLayout();
    // Tutte le colonne che si possono mostrare: [{key, title, field}].
    Q_INVOKABLE QVariantList availableColumns() const;
    // Il titolo di una colonna dalla sua chiave, anche se non e' mostrata.
    Q_INVOKABLE QString titleOf(const QString& key) const;
    // Il valore di una riga per chiave, anche se la colonna e' nascosta.
    Q_INVOKABLE QString valueFor(int row, const QString& key) const;
    bool filtered() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void refreshAfterInsert();
    bool busy() const { return m_busy; }
    QString sortKey() const { return m_sortKey; }
    bool sortAscending() const { return m_sortAscending; }
    // Ordina per quella colonna; di nuovo sulla stessa, al contrario.
    Q_INVOKABLE void sortBy(const QString& key);
    // L'ordine esatto (per ripristinare quello salvato).
    Q_INVOKABLE void setSort(const QString& key, bool ascending);
    Q_INVOKABLE void clearFilters();
    // Filtri come mappa, per salvarli con un nome e riapplicarli.
    Q_INVOKABLE QVariantMap filterState() const;
    Q_INVOKABLE void applyFilterState(const QVariantMap& state);
    Q_INVOKABLE qint64 idAt(int row) const;
    Q_INVOKABLE QString callAt(int row) const;
    Q_INVOKABLE QString valueAt(int row, int column) const;
    Q_INVOKABLE int rowForId(qint64 id) const;
    Q_INVOKABLE int columnWidthHint(int column) const;
    Q_INVOKABLE QString columnKey(int column) const;
    Q_INVOKABLE QString columnTitle(int column) const;
    // Bande e modi presenti nel log, per il menu dei filtri.
    Q_INVOKABLE QStringList bandsInLog() const;
    Q_INVOKABLE QStringList modesInLog() const;
    // Etichette del log con il conteggio: [{key, count}].
    Q_INVOKABLE QVariantList tagsInLog() const;
    // Gli id delle righe mostrate, nell'ordine della tabella (per export ed etichette).
    Q_INVOKABLE QVariantList shownIds() const;

    // Aggiunge una riga appena scritta, senza ricaricare tutto.
    void insertQso(qint64 id);
    // Rilegge una riga che e' cambiata (una conferma, il callbook che completa
    // un QSO): la tabella non si ricostruisce per una riga sola.
    void refreshQso(qint64 id);
    void reloadAfterEdit(qint64 id);
    // Gli upload QSL possono aggiornare piu' righe contemporaneamente.
    void reloadAfterQslUpdate();
    // Remove confirmed deletions without resetting a deeply scrolled view.
    void removeDeletedIds(const QList<qint64>& ids);

signals:
    void countChanged();
    void filtersChanged();
    void layoutChanged();
    void sortChanged();
    void busyChanged();
    void fieldValuesReady(const QString& key, const QVariantList& values);
    // [{log, utc, call, band, mode, country, qsl}] dal piu' recente, e per ogni
    // log {name, path, count, truncated, error}.
    void logsSearched(const QVariantList& rows, const QVariantList& perLog);

private:
    // Le cose nuove di ogni QSO, un bit per categoria: due byte, non una
    // stringa, per un milione di righe. Si tengono tutte e non solo la prima,
    // cosi' una categoria col colore spento non copre quelle dopo.
    QHash<qint64, quint16> m_category;
    QSet<QString> m_activeCategories;
    bool m_activeChosen{false};
    QString m_sortKey{QStringLiteral("utc")};
    bool m_sortAscending{false};
    bool defaultSort() const { return m_sortKey == QLatin1String("utc") && !m_sortAscending; }
    // Filtri e ordine come SQL.
    QString whereSql(QVariantList& binds, bool otherLogs = false) const;
    QString searchSql(QVariantList& binds) const;
    QString orderSql() const;
    QString m_categorySignature;   // il log com'era quando si sono contate (changeStamp)
    // Quello che si e' gia' visto, per le categorie di un QSO nuovo senza
    // ricontare il log: entita', continente, zone, locatore, nominativo, da
    // soli e per banda, come impronte. Valgono per i QSO piu' recenti
    // dell'ultimo contato.
    mutable QSet<quint64> m_seen[12];
    void adoptCategories(CategoryPass&& pass);
    // Su un log in un file la conta gira su un altro filo; i QSO arrivati nel
    // frattempo si guardano quando finisce.
    QThreadPool m_categoryPool;
    int m_categoryGeneration{0};
    bool m_categoryRunning{false};
    mutable QList<qint64> m_categoryLater;
    mutable QString m_seenLastOn;
    mutable qint64 m_seenLastId{0};
    mutable bool m_seenValid{false};

    struct Row {
        qint64  id{0};
        QString sortKey;
        QString values[ColumnCount];
        // Le colonne mostrate che non sono fra quelle di sempre, nell'ordine
        // di m_extra.
        QStringList extra;
        quint8 qsl{QslNone};
    };
    // La riga in quella posizione, letta con la sua pagina se non c'e' gia'.
    const Row* rowAt(int row) const;
    void dropPages();

    QString selectSql(const QString& where) const;
    // Le categorie di tutto il log in una volta (reload), o di un QSO solo.
    void computeCategories();
    quint16 categoryOf(qint64 id) const;
    Row rowFromQuery(const QSqlQuery& q) const;
    void refreshTotal();

    // La colonna mostrata in quella posizione, come valore: fra quelle di
    // sempre (core >= 0) o fra le altre (extra >= 0).
    struct Slot {
        int core{-1};
        int extra{-1};
    };
    void rebuildSlots();

    decolog::core::LogDatabase* m_db;
    // Le righe mostrate, nell'ordine della tabella.
    QVector<qint64> m_ids;
    // Evidenziata: l'ultima arrivata, quella che l'operatore cerca con lo sguardo.
    qint64 m_freshId{0};
    // Le pagine lette, e l'ordine in cui sono state usate (la prima e' la
    // prima a uscire).
    mutable QHash<int, QVector<Row>> m_pages;
    mutable QList<int> m_pageUse;
    QStringList m_layout;
    QStringList m_extra;        // le chiavi mostrate che non sono di sempre
    QVector<Slot> m_slots;
    QString m_filter;
    QStringList m_bands;
    QStringList m_modes;
    QString m_month;
    int m_dxcc{0};
    QString m_qsl;
    int m_profile{0};
    QString m_tag;
    QString m_dateFrom;
    QString m_dateTo;
    QVariantMap m_fields;
    int m_total{0};

    // Il ricarico su un altro filo: da quante righe in su, e l'ultimo chiesto
    // (quelli rimasti in coda si saltano, quelli arrivati tardi si scartano).
    static constexpr int kBackgroundRows = 50000;
    QThreadPool m_reloadPool;
    int m_reloadGeneration{0};
    std::shared_ptr<std::atomic<int>> m_reloadLatest{std::make_shared<std::atomic<int>>(0)};
    bool m_busy{false};
    QString idsSql(QVariantList& binds, bool scan) const;
    bool scanWholeLog() const;
    void reloadImpl(bool preserveIfUnchanged);
    void adoptIds(QVector<qint64>&& ids, bool preserveIfUnchanged);
};

} // namespace decolog::app
