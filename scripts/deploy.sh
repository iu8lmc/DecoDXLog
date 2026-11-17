#!/usr/bin/env bash
# DecoDXLog — crea una cartella autonoma, avviabile con doppio clic (Windows, MSYS2).
#
# Come in DecoRTTY, windeployqt da solo non basta. Qui si aggiunge:
#   * le librerie del compilatore MinGW e di terze parti (qtkeychain, OpenSSL,
#     ICU...), risolte ricorsivamente con ldd perche' anche le DLL copiate ne
#     hanno di proprie, plugin QML compresi;
#   * il plugin TLS: senza, QRZ.com e HamQTH (HTTPS) non rispondono, e il
#     programma non da' altro segno che "connessione fallita";
#   * il driver SQLite, senza il quale il log non si apre;
#   * l'albero dei moduli QML, perche' l'analisi degli import salta qualche
#     sottomodulo (QtQuick.Controls.impl, QtQuick.Dialogs.quickimpl);
#   * un qt.conf, senza il quale Qt cerca plugin e moduli dov'era installato.
#
# Alla fine crea anche l'archivio da passare a qualcuno: dist/DecoDXLog-<versione>-win64.zip.
#
#   scripts/deploy.sh            compila in build/ e prepara dist/
#   DIST=/c/tmp/decolog scripts/deploy.sh
#   BUILD=/c/decolog/build-dev scripts/deploy.sh    (se build/ e' in uso)
#   NO_ZIP=1 scripts/deploy.sh   solo la cartella
set -e

MINGW=${MINGW:-/c/msys64/mingw64}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD:-$ROOT/build}
DIST=${DIST:-$ROOT/dist}

export PATH="$MINGW/bin:$PATH"

echo "== build =="
cmake --build "$BUILD"

echo "== cartella $DIST =="
# Si svuota invece di rimuoverla: su Windows basta una shell aperta li' dentro
# perche' la rimozione fallisca in silenzio.
mkdir -p "$DIST"
find "$DIST" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
cp "$BUILD"/DecoDXLog.exe "$DIST"/
for extra in decodxlog_udpsend.exe decodxlog_clusterprobe.exe; do
    [ -f "$BUILD/$extra" ] && cp "$BUILD/$extra" "$DIST"/
done

# VOACAP: il motore (collegato statico, niente DLL Fortran) e i suoi dati, se
# la compilazione li ha fatti — servono gfortran e i coefficienti convertiti.
if [ -f "$BUILD/voacap/voacapl.exe" ] && [ -f "$BUILD/voacap/itshfbc/coeffs/coeff01w.bin" ]; then
    echo "== VOACAP =="
    mkdir -p "$DIST/voacap"
    cp "$BUILD/voacap/voacapl.exe" "$BUILD/voacap/LICENSE.txt" "$DIST/voacap/"
    cp -r "$BUILD/voacap/itshfbc" "$DIST/voacap/"
else
    echo "attenzione: VOACAP non compilato (manca gfortran): la previsione restera' quella semplice" >&2
fi

echo "== librerie Qt =="
( cd "$DIST" && windeployqt --qmldir "$ROOT/qml" --qmldir "$ROOT/libs/decodium-ui/qml" --release \
    --no-translations --no-system-d3d-compiler --no-opengl-sw \
    DecoDXLog.exe >/dev/null )

echo "== plugin TLS e SQLite =="
mkdir -p "$DIST/tls" "$DIST/sqldrivers" "$DIST/platforms"
# La piattaforma offscreen: serve alle schermate di prova (--show ... --grab) e a
# far partire il programma su una macchina senza sessione grafica.
cp -n "$MINGW/share/qt6/plugins/platforms/qoffscreen.dll" "$DIST/platforms/" 2>/dev/null || true
cp "$MINGW"/share/qt6/plugins/tls/*.dll "$DIST/tls/"
cp "$MINGW/share/qt6/plugins/sqldrivers/qsqlite.dll" "$DIST/sqldrivers/"
# Gli altri driver SQL trascinerebbero client MySQL, PostgreSQL e ODBC.
find "$DIST/sqldrivers" -name "*.dll" ! -name "qsqlite.dll" -delete

# La mappa del rotore usa QtLocation: il plugin delle mappe lo carica a runtime,
# quindi ldd non lo vede, esattamente come il TLS.
if [ -d "$MINGW/share/qt6/plugins/geoservices" ]; then
    mkdir -p "$DIST/geoservices"
    cp "$MINGW"/share/qt6/plugins/geoservices/*.dll "$DIST/geoservices/" 2>/dev/null || true
fi

echo "== moduli QML completi =="
rm -rf "$DIST/qml"
cp -r "$MINGW/share/qt6/qml" "$DIST/qml"
# I moduli dell'applicazione sono dentro l'eseguibile; gli strumenti per Qt
# Designer e i moduli 3D e multimediali non servono a un log.
rm -rf "$DIST/qml/DecoDXLog" "$DIST/qml/Decodium" "$DIST/qml/QtQuick/Controls/designer" \
       "$DIST/qml/QtQuick3D" "$DIST/qml/QtMultimedia" "$DIST/qml/QtWebEngine" "$DIST/qml/QtWebView"

cat > "$DIST/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = .
Imports = qml
Qml2Imports = qml
EOF

echo "== dipendenze, ricorsivamente =="
added=1
round=0
while [ "$added" -gt 0 ] && [ "$round" -lt 10 ]; do
    added=0
    round=$((round + 1))
    while read -r f; do
        while read -r dep; do
            [ -n "$dep" ] || continue
            # ldd scrive la libreria come la vede la shell: dentro MSYS2 e'
            # /mingw64/bin/..., da fuori /c/msys64/mingw64/bin/... Se si guarda
            # una scrittura sola, l'altra non trova niente e il pacchetto parte
            # senza libstdc++: non e' un errore, e' una cartella che non si apre.
            case "$dep" in
                /mingw64/*) dep="$MINGW${dep#/mingw64}" ;;
            esac
            [ -f "$dep" ] || continue
            base=$(basename "$dep")
            if [ ! -f "$DIST/$base" ]; then
                cp "$dep" "$DIST/"
                echo "  + $base"
                added=$((added + 1))
            fi
        done < <(ldd "$f" 2>/dev/null | grep -oiE "($MINGW|/mingw64)/bin/[^ ]+\.dll")
    done < <(find "$DIST" -type f \( -name "*.dll" -o -name "*.exe" \))
done

# OpenSSL non e' una dipendenza dichiarata: il plugin TLS lo carica a runtime,
# quindi ldd non lo vede. La cassaforte Cloud, invece, lo usa direttamente:
# l'installer deve contenere entrambe le DLL anche su una macchina che non ha
# MSYS2/OpenSSL installati.
openssl_major=""
for major in 4 3; do
    if [ -f "$MINGW/bin/libssl-$major-x64.dll" ] && [ -f "$MINGW/bin/libcrypto-$major-x64.dll" ]; then
        openssl_major="$major"
        break
    fi
done
if [ -z "$openssl_major" ]; then
    echo "OpenSSL non disponibile: manca una coppia libssl/libcrypto compatibile nel runtime MinGW" >&2
    exit 1
fi
for must in "libssl-$openssl_major-x64.dll" "libcrypto-$openssl_major-x64.dll"; do
    cp -n "$MINGW/bin/$must" "$DIST/"
    [ -f "$DIST/$must" ] || { echo "manca $must nel pacchetto Windows" >&2; exit 1; }
done

# Senza le librerie del compilatore la cartella non si apre, e Windows non dice
# quale manca: meglio fermarsi qui che spedire un archivio che non parte.
for must in libstdc++-6.dll libwinpthread-1.dll libgcc_s_seh-1.dll; do
    if [ ! -f "$DIST/$must" ]; then
        echo "manca $must: le dipendenze non si sono risolte (ldd)" >&2
        exit 1
    fi
done

echo "== licenze e istruzioni =="
cp "$ROOT/LICENSE" "$DIST/LICENSE.txt"
cp "$ROOT/resources/cty/COPYRIGHT.txt" "$DIST/cty.csv-COPYRIGHT.txt"
# Il decodificatore CW e' ggmorse, licenza MIT: la sua licenza va distribuita.
cp "$ROOT/libs/ggmorse/LICENSE" "$DIST/ggmorse-LICENSE.txt"
# L'icona accanto all'eseguibile serve a chi si crea un collegamento a mano.
cp "$ROOT/resources/decodxlog.ico" "$DIST/decodxlog.ico"

VERSION=$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
VERSION=${VERSION:-0.0.0}

cat > "$DIST/LEGGIMI.txt" <<EOF
DecoDXLog $VERSION — il log della stazione (famiglia Decodium)

Avvio
  Doppio clic su DecoDXLog.exe. Non serve installare niente: le librerie sono qui
  dentro. Il log e le impostazioni stanno in
  %APPDATA%\\Decodium (decodxlog.sqlite e DecoDXLog.ini); questa cartella si puo'
  spostare o cancellare senza perdere i QSO.

Primo avvio
  1. Setup -> Decodium link: la porta UDP su cui DecoDXLog ascolta i QSO
     (2237 di default; se un altro programma la usa gia', cambiala qui e
     aggiungila fra le destinazioni UDP di Decodium).
  2. Station profiles: nominativo e locatore della stazione.
  3. Setup -> QSL services: utente e password LoTW per scaricare le conferme.
  4. Ctrl+K: il DX cluster. In Fonti si accendono i nodi, RBN, HamAlert e POTA.

Cosa c'e' dentro
  DecoDXLog.exe               l'applicazione
  decodxlog_udpsend.exe       finge di essere Decodium e manda un QSO di prova
  decodxlog_clusterprobe.exe  prova una fonte di spot da riga di comando
  decodxlog.ico               l'icona, per crearsi un collegamento
  voacap\                     VOACAP, il motore di propagazione dell'ITS

Licenza
  GPL-3.0 (LICENSE.txt). L'elenco delle entita' DXCC e' il cty.csv di AD1C
  (cty.csv-COPYRIGHT.txt). Il decodificatore CW e' ggmorse di Georgi Gerganov,
  licenza MIT (ggmorse-LICENSE.txt). VOACAP e' dell'NTIA/ITS, dominio
  pubblico; la versione voacapl di J. A. Watson e' CC0 (voacap\LICENSE.txt).
EOF

if [ -z "${NO_ZIP:-}" ]; then
    echo "== archivio =="
    ARCHIVE="$DIST/../DecoDXLog-$VERSION-win64.zip"
    rm -f "$ARCHIVE"
    # La cartella entra nell'archivio con il suo nome, cosi' chi lo apre non si
    # ritrova cinquanta DLL sparse nel desktop.
    STAGE=$(mktemp -d)
    cp -r "$DIST" "$STAGE/DecoDXLog-$VERSION"
    if command -v 7z >/dev/null; then
        ( cd "$STAGE" && 7z a -tzip -mx=7 "$ARCHIVE" "DecoDXLog-$VERSION" >/dev/null )
    elif command -v zip >/dev/null; then
        ( cd "$STAGE" && zip -qr9 "$ARCHIVE" "DecoDXLog-$VERSION" )
    else
        powershell -NoProfile -Command \
            "Compress-Archive -Path '$(cygpath -w "$STAGE")\\DecoDXLog-$VERSION' -DestinationPath '$(cygpath -w "$ARCHIVE")' -Force"
    fi
    rm -rf "$STAGE"
fi

echo
echo "pronto: $DIST"
echo "  DecoDXLog.exe               l'applicazione"
echo "  decodxlog_udpsend.exe       finge di essere Decodium, per le prove"
echo "  decodxlog_clusterprobe.exe  prova una fonte di spot"
du -sh "$DIST"
[ -f "${ARCHIVE:-}" ] && ls -lh "$ARCHIVE" | awk '{print "archivio: " $9 " (" $5 ")"}'
exit 0
