-- DecoLog Desktop — schema SQLite v13
-- Principi: nomi ADIF, lossless (adif_extra), campi di sync su ogni QSO, UTC.

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE schema_version (
    version     INTEGER NOT NULL,
    applied_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
INSERT INTO schema_version (version) VALUES (13);

CREATE TABLE operator_profile (
    id INTEGER PRIMARY KEY,
    callsign TEXT NOT NULL UNIQUE,
    display_name TEXT,
    updated_at TEXT NOT NULL
);

CREATE TABLE operating_location (
    id INTEGER PRIMARY KEY,
    location_key TEXT NOT NULL UNIQUE,
    label TEXT,
    gridsquare TEXT,
    dxcc INTEGER,
    updated_at TEXT NOT NULL
);

CREATE TABLE dxcc_entity (
    id INTEGER PRIMARY KEY,
    name TEXT,
    updated_at TEXT NOT NULL
);

-- Profili stazione (portatile, casa, evento speciale...)
CREATE TABLE station_profile (
    id                  INTEGER PRIMARY KEY,
    uuid                TEXT    NOT NULL UNIQUE,
    name                TEXT    NOT NULL,          -- es. "Casa JN71DC"
    station_callsign    TEXT    NOT NULL,          -- STATION_CALLSIGN
    operator            TEXT,                      -- OPERATOR
    my_gridsquare       TEXT,                      -- MY_GRIDSQUARE
    my_cq_zone          INTEGER,
    my_itu_zone         INTEGER,
    my_dxcc             INTEGER,
    my_rig              TEXT,
    my_antenna          TEXT,
    default_tx_pwr      REAL,                      -- W
    lotw_station_loc    TEXT,                      -- nome Station Location in TQSL
    award_account       TEXT,                      -- account diploma storico
    operating_location  TEXT,                      -- luogo operativo dichiarato
    valid_from          TEXT,                      -- yyyy-MM-dd
    valid_to            TEXT,                      -- yyyy-MM-dd
    operator_profile_id INTEGER REFERENCES operator_profile(id),
    operating_location_id INTEGER REFERENCES operating_location(id),
    dxcc_entity_id      INTEGER REFERENCES dxcc_entity(id),
    is_default          INTEGER NOT NULL DEFAULT 0,
    revision            INTEGER NOT NULL DEFAULT 1,
    updated_at          TEXT    NOT NULL,
    deleted             INTEGER NOT NULL DEFAULT 0,
    dirty               INTEGER NOT NULL DEFAULT 1
);

-- QSO
CREATE TABLE qso (
    id                  INTEGER PRIMARY KEY,
    uuid                TEXT    NOT NULL UNIQUE,   -- generato dal client, idempotenza sync
    station_profile_id  INTEGER REFERENCES station_profile(id),

    -- core ADIF
    call                TEXT    NOT NULL,          -- CALL
    qso_datetime_on     TEXT    NOT NULL,          -- QSO_DATE+TIME_ON, ISO-8601 UTC
    qso_datetime_off    TEXT,                      -- QSO_DATE_OFF+TIME_OFF
    band                TEXT    NOT NULL,          -- BAND (minuscolo: "40m")
    band_rx             TEXT,
    freq                REAL,                      -- FREQ, MHz
    freq_rx             REAL,
    mode                TEXT    NOT NULL,          -- MODE  (FT2 -> MFSK)
    submode             TEXT,                      -- SUBMODE (FT2 -> FT2)
    rst_sent            TEXT,
    rst_rcvd            TEXT,
    gridsquare          TEXT,
    name                TEXT,
    qth                 TEXT,
    country             TEXT,
    dxcc                INTEGER,
    cqz                 INTEGER,
    ituz                INTEGER,
    cont                TEXT,
    state               TEXT,
    cnty                TEXT,
    iota                TEXT,
    sota_ref            TEXT,
    pota_ref            TEXT,
    wwff_ref            TEXT,
    sig                 TEXT,
    sig_info            TEXT,
    prop_mode           TEXT,
    sat_name            TEXT,
    sat_mode            TEXT,
    tx_pwr              REAL,
    comment             TEXT,
    notes               TEXT,
    tags                TEXT,                      -- APP_DECOLOG_TAGS, etichette separate da virgola (v2)

    -- origine
    source              TEXT    NOT NULL DEFAULT 'manual', -- manual | udp_decodium | udp_wsjtx | import | cloud
    source_app          TEXT,                      -- es. "Decodium 4.0.xxx"

    adif_extra          TEXT,                      -- JSON {"FIELD":"value"} campi non mappati

    -- sync
    revision            INTEGER NOT NULL DEFAULT 1,
    created_at          TEXT    NOT NULL,
    updated_at          TEXT    NOT NULL,
    deleted             INTEGER NOT NULL DEFAULT 0,  -- soft delete
    dirty               INTEGER NOT NULL DEFAULT 1   -- 1 = da inviare al cloud
);

CREATE INDEX idx_qso_call      ON qso(call);
CREATE INDEX idx_qso_datetime  ON qso(qso_datetime_on);
CREATE INDEX idx_qso_band_mode ON qso(band, mode, submode);
CREATE INDEX idx_qso_dedup     ON qso(call, band, mode, submode, qso_datetime_on);
CREATE INDEX idx_qso_dirty     ON qso(dirty) WHERE dirty = 1;
CREATE INDEX idx_qso_dxcc      ON qso(dxcc);
CREATE INDEX idx_qso_grid      ON qso(gridsquare);
CREATE INDEX idx_qso_submode   ON qso(submode);
CREATE INDEX idx_qso_live_time ON qso(qso_datetime_on) WHERE deleted = 0;
CREATE INDEX idx_qso_tags      ON qso(tags) WHERE IFNULL(tags, '') <> '';
CREATE INDEX idx_qso_profile   ON qso(station_profile_id) WHERE deleted = 0;

-- Stato QSL per servizio (una riga per QSO x servizio)
CREATE TABLE qsl_status (
    qso_id      INTEGER NOT NULL REFERENCES qso(id) ON DELETE CASCADE,
    service     TEXT    NOT NULL,   -- lotw | qrz | clublog | eqsl | card
    sent        TEXT    NOT NULL DEFAULT 'N',  -- ADIF: Y N R Q I
    sent_date   TEXT,
    rcvd        TEXT    NOT NULL DEFAULT 'N',
    rcvd_date   TEXT,
    remote_id   TEXT,               -- id lato servizio, se esiste
    via         TEXT,               -- QSL cartacee: B bureau, D diretta, E elettronica
    last_error  TEXT,
    PRIMARY KEY (qso_id, service)
);
CREATE INDEX idx_qsl_pending ON qsl_status(service, sent);

-- Allegati QSL ricevuti: un file, molti QSO, contenuto privato nel backup DB.
CREATE TABLE qsl_attachment (
    id          INTEGER PRIMARY KEY,
    sha256      TEXT NOT NULL UNIQUE,
    filename    TEXT NOT NULL,
    mime        TEXT NOT NULL,
    size        INTEGER NOT NULL,
    data        BLOB NOT NULL,
    created_at  TEXT NOT NULL
);
CREATE TABLE qsl_attachment_qso (
    attachment_id INTEGER NOT NULL REFERENCES qsl_attachment(id) ON DELETE CASCADE,
    qso_id        INTEGER NOT NULL REFERENCES qso(id) ON DELETE CASCADE,
    PRIMARY KEY (attachment_id, qso_id)
);
CREATE INDEX idx_qsl_attachment_qso ON qsl_attachment_qso(qso_id);

-- Storico versioni (conflitti di sync e annullamento modifiche)
CREATE TABLE qso_history (
    id          INTEGER PRIMARY KEY,
    qso_uuid    TEXT    NOT NULL,
    revision    INTEGER NOT NULL,
    snapshot    TEXT    NOT NULL,   -- JSON completo del QSO
    reason      TEXT    NOT NULL,   -- edit | conflict_lost | delete
    recorded_at TEXT    NOT NULL
);
CREATE INDEX idx_hist_uuid ON qso_history(qso_uuid);

-- Provenienza dei dati aggiunti da callbook o altri arricchitori.
CREATE TABLE qso_enrichment (
    id              INTEGER PRIMARY KEY,
    qso_id          INTEGER NOT NULL REFERENCES qso(id) ON DELETE CASCADE,
    field           TEXT NOT NULL,
    previous_value  TEXT,
    value           TEXT NOT NULL,
    source          TEXT NOT NULL,
    kind            TEXT NOT NULL,
    acquired_at     TEXT NOT NULL,
    valid_from      TEXT,
    valid_to        TEXT
);
CREATE INDEX idx_enrichment_qso ON qso_enrichment(qso_id, field);

-- Stato sync per account cloud
CREATE TABLE sync_state (
    account         TEXT PRIMARY KEY,  -- es. utente@decolog
    device_id       TEXT NOT NULL,
    pull_cursor     TEXT,              -- cursore opaco dal server
    last_push_at    TEXT,
    last_pull_at    TEXT,
    last_error      TEXT
);

-- Impostazioni applicative non legate al profilo QSettings
CREATE TABLE app_setting (
    key     TEXT PRIMARY KEY,
    value   TEXT
);

-- Journal for committed imports; entries use UUID as well as local ID.
CREATE TABLE import_batch (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL
);
CREATE TABLE import_qso (
    batch_id INTEGER NOT NULL REFERENCES import_batch(id),
    qso_id INTEGER NOT NULL,
    qso_uuid TEXT NOT NULL,
    imported_revision INTEGER NOT NULL,
    baseline_hash TEXT,
    PRIMARY KEY (batch_id, qso_uuid)
);

-- Risposte API idempotenti, persistenti attraverso riavvii e retry.
CREATE TABLE api_idempotency (
    key          TEXT PRIMARY KEY,
    request_body BLOB NOT NULL,
    status       INTEGER NOT NULL,
    content_type TEXT NOT NULL,
    response_body BLOB NOT NULL,
    created_at   TEXT NOT NULL
);
CREATE INDEX idx_api_idempotency_created ON api_idempotency(created_at);
