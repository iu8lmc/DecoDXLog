"""DecoDXLog Cloud — il log dal browser.

Le pagine sono in sola lettura e sono di chi entra: queste prove guardano che
non si veda niente senza accesso, che la ricerca cerchi davvero e che l'ADIF
esca com'e' entrato.
"""

from __future__ import annotations

import datetime as dt
import os
import tempfile

import pytest

os.environ.setdefault("DECOLOG_DATABASE_URL", "sqlite:///" + os.path.join(tempfile.mkdtemp(), "w.sqlite"))

from fastapi.testclient import TestClient  # noqa: E402

from decolog_cloud import models  # noqa: E402
from decolog_cloud.main import app  # noqa: E402

from .helpers import approve  # noqa: E402


@pytest.fixture()
def client(tmp_path):
    url = "sqlite:///" + str(tmp_path / "cloud.sqlite").replace("\\", "/")
    engine = models.create_engine(url, future=True, connect_args={"check_same_thread": False})
    models.engine = engine
    models.SessionLocal.configure(bind=engine)
    models.Base.metadata.create_all(engine)
    with TestClient(app) as c:
        yield c


PASSWORD = "una password lunga"


def account(client, callsign="IU8LMC"):
    reply = client.post("/v1/auth/signup", json={"callsign": callsign, "password": PASSWORD})
    assert reply.status_code == 200
    approve(client, callsign)
    return {"Authorization": "Bearer " + reply.json()["token"]}


def qso(uuid, call="DL9ZZT", when="2026-09-18T07:00:00Z", **fields):
    return {
        "uuid": uuid,
        "revision": 1,
        "call": call,
        "band": "20m",
        "mode": "MFSK",
        "submode": "FT2",
        "startedAt": when,
        "fields": {"CALL": call, "QSO_DATE": "20260918", "TIME_ON": "070000",
                   "BAND": "20m", "MODE": "MFSK", "SUBMODE": "FT2", "RST_SENT": "599", **fields},
    }


def sign_in(client, callsign="IU8LMC"):
    reply = client.post("/login", data={"callsign": callsign, "password": PASSWORD},
                        follow_redirects=False)
    assert reply.status_code == 303
    assert reply.headers["location"] == "/log"
    return reply


def test_without_a_session_there_is_nothing_to_see(client):
    account(client)
    home = client.get("/", follow_redirects=False)
    assert home.status_code == 200
    assert "Nominativo" in home.text

    for path in ("/log", "/qso/u1", "/export.adi"):
        reply = client.get(path, follow_redirects=False)
        assert reply.status_code in (303, 404)
        if reply.status_code == 303:
            assert reply.headers["location"] == "/"
    assert client.get("/log/rows").status_code == 401


def test_wrong_password_says_the_same_thing_as_an_unknown_call(client):
    account(client)
    bad = client.post("/login", data={"callsign": "IU8LMC", "password": "sbagliata!"})
    unknown = client.post("/login", data={"callsign": "XX0XXX", "password": PASSWORD})
    assert bad.status_code == unknown.status_code == 401
    assert "non validi" in bad.text and "non validi" in unknown.text


def test_the_log_shows_the_qsos(client):
    headers = account(client)
    client.post("/v1/sync/push", json={"qsos": [qso("u1", "DL9ZZT", NAME="Klaus"),
                                                qso("u2", "EA5XYZ", when="2026-09-18T08:00:00Z")]},
                headers=headers)
    sign_in(client)

    page = client.get("/log")
    assert page.status_code == 200
    assert "DL9ZZT" in page.text
    assert "EA5XYZ" in page.text
    assert "Klaus" in page.text
    # Il totale sta nella testata del pannello, come nella finestra.
    assert "2 QSO" in page.text


def test_search_filters_the_rows(client):
    headers = account(client)
    client.post("/v1/sync/push", json={"qsos": [qso("u1", "DL9ZZT"), qso("u2", "EA5XYZ")]},
                headers=headers)
    sign_in(client)

    rows = client.get("/log/rows", params={"q": "ea5"})
    assert "EA5XYZ" in rows.text
    assert "DL9ZZT" not in rows.text

    by_band = client.get("/log/rows", params={"band": "40m"})
    assert "DL9ZZT" not in by_band.text


def test_the_detail_page_shows_every_field(client):
    headers = account(client)
    client.post("/v1/sync/push", json={"qsos": [qso("u1", GRIDSQUARE="JO62", COMMENT="bel segnale")]},
                headers=headers)
    sign_in(client)

    page = client.get("/qso/u1")
    assert page.status_code == 200
    assert "JO62" in page.text
    assert "bel segnale" in page.text
    assert "GRIDSQUARE" in page.text


def test_export_gives_back_adif(client):
    headers = account(client)
    client.post("/v1/sync/push", json={"qsos": [qso("u1", "DL9ZZT")]}, headers=headers)
    sign_in(client)

    reply = client.get("/export.adi")
    assert reply.status_code == 200
    assert "IU8LMC-cloud.adi" in reply.headers["content-disposition"]
    assert "<EOH>" in reply.text
    assert "<CALL:6>DL9ZZT" in reply.text
    assert reply.text.rstrip().endswith("<EOR>")


def test_export_can_select_the_station_profile(client):
    headers = account(client)
    client.post("/v1/sync/push", json={"qsos": [
        qso("u1", "DL9ZZT", STATION_CALLSIGN="IU8LMC"),
        qso("u2", "W1AW", STATION_CALLSIGN="IU8LMC/P"),
    ]}, headers=headers)
    sign_in(client)

    reply = client.get("/export.adi", params={"profile": "IU8LMC/P"})
    assert reply.status_code == 200
    assert "IU8LMC/P-cloud.adi" in reply.headers["content-disposition"]
    assert "<CALL:4>W1AW" in reply.text
    assert "DL9ZZT" not in reply.text


def test_one_account_does_not_see_another(client):
    mine = account(client, "IU8LMC")
    account(client, "DL9ZZT")
    client.post("/v1/sync/push", json={"qsos": [qso("u1", "W1AW")]}, headers=mine)

    sign_in(client, "DL9ZZT")
    page = client.get("/log")
    assert "W1AW" not in page.text
    assert "0 QSO" in page.text


def test_logout_closes_the_door(client):
    account(client)
    sign_in(client)
    assert client.get("/log").status_code == 200

    client.post("/logout", follow_redirects=False)
    assert client.get("/log", follow_redirects=False).headers["location"] == "/"


def test_the_station_page_shows_profiles_and_settings(client):
    headers = account(client)
    client.post(
        "/v1/sync/push",
        json={
            "docs": [
                {"kind": "profile", "key": "p1", "revision": 1,
                 "data": {"name": "Casa", "stationCallsign": "IU8LMC", "myGridsquare": "JN70",
                          "myRig": "FT-991A", "isDefault": True}},
                {"kind": "setting", "key": "station", "revision": 3,
                 "data": {"ui/language": "it", "theme/current": "Darkcodium", "udp/port": 2238,
                          "layout/savedFilters": {"__qvariant__": "AAAACAAAAAAA"}}},
            ]
        },
        headers=headers,
    )
    sign_in(client)

    page = client.get("/station")
    assert page.status_code == 200
    assert "Casa" in page.text
    assert "JN70" in page.text
    assert "FT-991A" in page.text
    assert "predefinito" in page.text
    assert "theme/current" in page.text and "Darkcodium" in page.text
    # Anche le porte: la stazione dev'essere la stessa, non una che le somiglia.
    assert "udp/port" in page.text and "2238" in page.text
    # Un filtro salvato non e' testo: si dice cos'e', non si vomita il blob.
    assert "valore interno di Qt" in page.text
    assert "AAAACAAAAAAA" not in page.text
    assert "revisione 3" in page.text


def test_the_station_page_needs_a_session(client):
    account(client)
    reply = client.get("/station", follow_redirects=False)
    assert reply.status_code == 303 and reply.headers["location"] == "/"


# ── Le pagine che rifanno le finestre del programma ───────────────────────────


def logged_qsos(client, headers):
    """Un pugno di QSO con dentro quello che serve a statistiche e diplomi."""
    client.post(
        "/v1/sync/push",
        json={"qsos": [
            qso("u1", "DL9ZZT", when="2026-09-18T07:00:00Z", DXCC="230", COUNTRY="Germany",
                CONT="EU", CQZ="14", GRIDSQUARE="JO62", LOTW_QSL_RCVD="Y", LOTW_QSL_SENT="Y"),
            qso("u2", "W1AW", when="2026-09-18T14:00:00Z", DXCC="291", COUNTRY="United States",
                CONT="NA", CQZ="5", STATE="CT", GRIDSQUARE="FN31"),
            qso("u3", "EA5XYZ", when="2025-01-02T23:00:00Z", DXCC="281", COUNTRY="Spain",
                CONT="EU", CQZ="14", GRIDSQUARE="IM98", QSL_RCVD="Y", QSL_SENT="Y"),
        ]},
        headers=headers,
    )


def test_every_page_needs_a_session(client):
    account(client)
    for path in ("/stats", "/awards", "/qsl", "/map"):
        reply = client.get(path, follow_redirects=False)
        assert reply.status_code == 303 and reply.headers["location"] == "/", path


def test_the_stats_page_shows_the_same_numbers_as_the_window(client):
    headers = account(client)
    logged_qsos(client, headers)
    sign_in(client)

    page = client.get("/stats")
    assert page.status_code == 200
    assert "Statistiche" in page.text
    assert ">3</b>" in page.text.replace(" ", "")     # tre QSO
    assert "2026" in page.text and "2025" in page.text
    assert "Banda per ora" in page.text
    # Il filtro per modo passa dall'indirizzo, cosi' il link si puo' salvare.
    assert client.get("/stats", params={"mode": "CW"}).status_code == 200


def test_the_awards_page_counts_worked_and_confirmed(client):
    headers = account(client)
    logged_qsos(client, headers)
    sign_in(client)

    page = client.get("/awards")
    assert page.status_code == 200
    assert "DXCC" in page.text and "WAZ" in page.text and "WAS" in page.text
    assert "Germany" in page.text          # l'entita' col suo nome
    assert "Connecticut" in page.text or "CT" in page.text
    # Due conferme su tre entita' lavorate: il riquadro dice confermati / lavorati.
    assert "/ 3" in page.text
    assert "Cosa manca" in page.text


def test_the_awards_page_shows_the_japanese_awards(client):
    headers = account(client)
    client.post("/v1/sync/push", headers=headers, json={"qsos": [
        qso("ja-1", call="JA1ABC", DXCC="339", CONT="AS", STATE="12", CNTY="1001"),
        qso("ja-2", call="JA3XYZ", DXCC="339", CONT="AS", STATE="25", CNTY="25007"),
    ]})
    sign_in(client)

    page = client.get("/awards")
    assert page.status_code == 200
    # Gli stessi diplomi del programma, col numero JARL e la prefettura.
    for name in ("WAC", "WAJA", "AJD", "JCC", "JCG"):
        assert name in page.text
    assert "1001" in page.text and "Tokyo" in page.text
    assert "25007" in page.text and "Osaka" in page.text


def test_the_awards_page_lets_you_choose_the_confirmations(client):
    headers = account(client)
    client.post("/v1/sync/push",
                json={"qsos": [qso("u1", "DL9ZZT", DXCC="230", EQSL_QSL_RCVD="Y")]},
                headers=headers)
    sign_in(client)

    without = client.get("/awards", params={"lotw": 1, "card": 1, "eqsl": 0})
    with_eqsl = client.get("/awards", params={"lotw": 1, "card": 1, "eqsl": 1})
    assert "eQSL" in without.text
    # Senza eQSL non c'e' niente di confermato; con eQSL sì.
    assert with_eqsl.text != without.text


def test_the_qsl_page_says_what_went_out_and_came_back(client):
    headers = account(client)
    logged_qsos(client, headers)
    sign_in(client)

    page = client.get("/qsl")
    assert page.status_code == 200
    assert "LoTW" in page.text and "Cartolina" in page.text
    assert "Ultime conferme" in page.text
    assert "DL9ZZT" in page.text
    # Club Log non ha un "ricevuto": si dice, invece di contare zero.
    assert "n/d" in page.text


def test_the_map_page_carries_the_worked_grids(client):
    headers = account(client)
    logged_qsos(client, headers)
    sign_in(client)

    page = client.get("/map")
    assert page.status_code == 200
    assert "JO62" in page.text and "FN31" in page.text and "IM98" in page.text
    assert "3 locatori" in page.text


def test_the_window_is_the_one_of_the_program(client):
    headers = account(client)
    logged_qsos(client, headers)
    sign_in(client)

    page = client.get("/log")
    assert page.status_code == 200
    # Le tre colonne: scheda del QSO, log, scheda del nominativo con FT2 e mappa.
    for piece in ("Scheda QSO", "Log", "Scheda nominativo", "FT2 Award", "Mappa"):
        assert piece in page.text, piece
    # Le schede in basso, nell'ordine del programma.
    for tab in ("Diplomi", "Statistiche", "Invio QSL", "Registro attività", "DX Cluster"):
        assert tab in page.text, tab
    # La barra di stato.
    assert "QSO: 3" in page.text


def test_choosing_a_qso_moves_the_side_panels(client):
    headers = account(client)
    logged_qsos(client, headers)
    sign_in(client)

    page = client.get("/log", params={"sel": "u2"})
    assert "W1AW" in page.text
    # La scheda del nominativo segue il QSO scelto: dice il suo paese.
    assert "United States" in page.text


def test_the_page_wears_the_theme_of_the_station(client):
    headers = account(client)
    client.post("/v1/sync/push",
                json={"docs": [{"kind": "setting", "key": "station", "revision": 1,
                                "data": {"theme/current": "Darkcodium",
                                         "theme/accentVariant": "amber"}}]},
                headers=headers)
    sign_in(client)

    page = client.get("/log")
    # Il tema e' quello sincronizzato dal programma, non uno deciso qui.
    assert "--bg-deep: #050706;" in page.text
    assert "--accent: #ffb820;" in page.text
    assert "Darkcodium · amber" in page.text


def test_the_cluster_tab_shows_the_sources_of_the_station(client):
    headers = account(client)
    client.post(
        "/v1/sync/push",
        json={"docs": [{"kind": "setting", "key": "station", "revision": 1, "data": {
            "cluster/sources": '[{"name": "iq8do (DX Spider)", "host": "iq8do.aricaserta.it",'
                               ' "port": 7300, "type": "cluster", "enabled": true, "login": "IU8LMC"}]'}}]},
        headers=headers,
    )
    sign_in(client)

    page = client.get("/cluster")
    assert page.status_code == 200
    assert "iq8do" in page.text and "7300" in page.text


def test_the_activity_tab_shows_what_arrived(client):
    headers = account(client)
    logged_qsos(client, headers)
    sign_in(client)

    page = client.get("/activity")
    assert page.status_code == 200
    assert "DL9ZZT" in page.text
    assert "Ultime modifiche" in page.text


def test_the_propagation_tab_shows_the_numbers_of_the_source(client, monkeypatch):
    from decolog_cloud import solar

    solar.reset_cache()
    monkeypatch.setattr(solar, "_download", lambda: SOLAR_XML)

    account(client)
    sign_in(client)
    page = client.get("/propagation")
    assert page.status_code == 200
    assert "Propagazione" in page.text
    assert "168" in page.text          # SFI
    assert "Band Closed" in page.text  # una condizione com'e' scritta dalla fonte
    assert "N0NBH" in page.text


def test_the_propagation_tab_does_not_fall_over_when_the_source_is_down(client, monkeypatch):
    from decolog_cloud import solar

    solar.reset_cache()

    def broken():
        raise OSError("giu'")

    monkeypatch.setattr(solar, "_download", broken)

    account(client)
    sign_in(client)
    page = client.get("/propagation")
    assert page.status_code == 200
    assert "non risponde" in page.text


def test_the_paper_qsl_queue_is_on_the_qsl_tab(client):
    headers = account(client)
    client.post(
        "/v1/sync/push",
        json={"qsos": [
            qso("u1", "DL9ZZT", QSL_SENT="Q"),                      # in coda
            qso("u2", "EA5XYZ", QSL_SENT="Y", QSL_SENT_VIA="B"),    # mandata, bureau
            qso("u3", "W1AW", QSL_SENT="Y", QSL_RCVD="Y", QSL_SENT_VIA="D"),
        ]},
        headers=headers,
    )
    sign_in(client)

    page = client.get("/qsl")
    assert page.status_code == 200
    assert "QSL di carta" in page.text
    assert "DL9ZZT" in page.text        # quella in coda
    assert "bureau" in page.text and "diretta" in page.text
    # Le colonne sono quelle del programma.
    assert "Da mandare" in page.text and "Confermate" in page.text


SOLAR_XML = b"""<solar><solardata>
  <source>N0NBH</source><updated>18 Sep 2026 0730 GMT</updated>
  <solarflux>168</solarflux><aindex>7</aindex><kindex>3</kindex>
  <sunspots>142</sunspots><aurora>3</aurora>
  <calculatedconditions>
    <band name="80m-40m" time="day">Fair</band>
    <band name="80m-40m" time="night">Good</band>
    <band name="12m-10m" time="night">Band Closed</band>
  </calculatedconditions>
</solardata></solar>"""


def test_settings_can_be_changed_from_the_browser(client):
    headers = account(client)
    client.post("/v1/sync/push",
                json={"docs": [{"kind": "setting", "key": "station", "revision": 1,
                                "data": {"theme/current": "Ocean Blue", "udp/port": 2238}}]},
                headers=headers)
    sign_in(client)

    reply = client.post("/station/settings",
                        data={"theme.current": "Darkcodium", "theme.accentVariant": "amber"},
                        follow_redirects=False)
    assert reply.status_code == 303 and reply.headers["location"] == "/station"

    # Il programma se lo riprende con il pull: revisione piu' alta, e quello che
    # non si tocca resta com'era.
    pulled = client.get("/v1/sync/pull", params={"since": 0}, headers=headers).json()
    doc = next(d for d in pulled["docs"] if d["kind"] == "setting")
    assert doc["revision"] == 2
    assert doc["data"]["theme/current"] == "Darkcodium"
    assert doc["data"]["theme/accentVariant"] == "amber"
    assert doc["data"]["udp/port"] == 2238
    assert doc["device"] == "browser"


def test_the_browser_cannot_write_what_it_should_not(client):
    headers = account(client)
    client.post("/v1/sync/push",
                json={"docs": [{"kind": "setting", "key": "station", "revision": 1,
                                "data": {"udp/port": 2238}}]},
                headers=headers)
    sign_in(client)

    # Una porta, un percorso, un valore fuori elenco: non passano.
    client.post("/station/settings",
                data={"udp.port": "9999", "backup.folder": "/tmp",
                      "theme.current": "Tema Inventato"},
                follow_redirects=False)

    pulled = client.get("/v1/sync/pull", params={"since": 0}, headers=headers).json()
    doc = next(d for d in pulled["docs"] if d["kind"] == "setting")
    assert doc["data"]["udp/port"] == 2238        # invariata
    assert "backup/folder" not in doc["data"]
    assert doc["data"].get("theme/current") != "Tema Inventato"
    assert doc["revision"] == 1                    # niente da cambiare, niente revisione


def test_changing_the_settings_needs_a_session(client):
    account(client)
    reply = client.post("/station/settings", data={"theme.current": "Darkcodium"},
                        follow_redirects=False)
    assert reply.status_code == 303 and reply.headers["location"] == "/"


def test_the_station_page_offers_the_choices(client):
    headers = account(client)
    client.post("/v1/sync/push",
                json={"docs": [{"kind": "setting", "key": "station", "revision": 1,
                                "data": {"theme/current": "Darkcodium"}}]},
                headers=headers)
    sign_in(client)

    page = client.get("/station")
    # La pagina e' la stessa finestra del programma: le pagine a sinistra hanno
    # gli stessi nomi, nello stesso ordine.
    for voice in ("Generale", "Tema e densita", "Collegamento a Decodium",
                  "Sync e Cloud", "Servizi QSL", "Callbook", "Radio (CAT)",
                  "Rotore", "Copie di sicurezza"):
        assert voice in page.text
    assert 'name="theme.current"' in page.text
    assert "Stellar Light" in page.text        # le altre scelte ci sono
    assert "Applica" in page.text
    # E quello che non sta in nessuna pagina resta comunque visibile.
    assert "Tutte le voci" in page.text


def test_the_contest_page_counts_the_score(client):
    headers = account(client)
    # Le ultime ore davvero: con una data fissa la prova smetteva di passare
    # una settimana dopo.
    now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
    first = (now - dt.timedelta(hours=2)).isoformat().replace("+00:00", "Z")
    second = (now - dt.timedelta(hours=1, minutes=50)).isoformat().replace("+00:00", "Z")
    client.post("/v1/sync/push", headers=headers, json={"qsos": [
        qso("c-1", call="DL9ZZT", when=first, DXCC="230", CQZ="14", MODE="CW"),
        qso("c-2", call="EA5XYZ", when=second, DXCC="281", CQZ="14", MODE="CW"),
    ]})
    sign_in(client)

    page = client.get("/contest", params={"hours": 168})
    assert page.status_code == 200
    assert "Punteggio" in page.text
    assert "Moltiplicatori" in page.text
    assert "Stima generica" in page.text
    assert "non applica i regolamenti specifici" in page.text
    # Due QSO, due entita': quattro punti di punteggio.
    assert ">2<" in page.text
