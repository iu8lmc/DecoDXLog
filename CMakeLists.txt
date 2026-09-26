cmake_minimum_required(VERSION 3.21)

project(DecoDXLog
    VERSION 1.16.16
    DESCRIPTION "Station logbook for the Decodium family"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(DECODXLOG_BUILD_TESTS "Build the unit tests" ON)
option(DECODXLOG_BUILD_TOOLS "Build the command-line tools (UDP simulator)" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Sql Gui Qml Quick QuickControls2 Multimedia WebSockets Positioning Location SerialPort LinguistTools)
qt_standard_project_setup(REQUIRES 6.5)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

# ── decodium-ui ───────────────────────────────────────────────────────────────
# Tema e componenti della famiglia Decodium. Sta in libs/ perche' e' pensato per
# uscire da qui e diventare un modulo condiviso con Decodium e DecoRTTY (Fase 0,
# §4.2): nulla la' dentro sa di DecoDXLog.
add_subdirectory(libs/decodium-ui)

# ── ggmorse ───────────────────────────────────────────────────────────────────
# Il decodificatore Morse di Georgi Gerganov (MIT), copiato in libs/ggmorse:
# otto file, nessuna dipendenza. GGMORSE_QUIET gli toglie le stampe su stdout —
# il testo ce lo prendiamo da takeRxData().
add_library(ggmorse STATIC
    libs/ggmorse/src/ggmorse.cpp
    libs/ggmorse/src/resampler.cpp
)
target_include_directories(ggmorse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/ggmorse/include)
target_compile_definitions(ggmorse PRIVATE GGMORSE_QUIET)
set_target_properties(ggmorse PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── Core ──────────────────────────────────────────────────────────────────────
# Niente GUI: ADIF, database e protocollo UDP si provano da soli e serviranno
# anche a strumenti senza finestra (import da riga di comando, sync).
add_library(decolog_core STATIC
    src/core/Activation.cpp
    src/core/Activation.h
    src/core/Adif.cpp
    src/core/Adif.h
    src/core/Awards.cpp
    src/core/Awards.h
    src/core/Bands.cpp
    src/core/Bands.h
    src/core/Cabrillo.cpp
    src/core/Dates.cpp
    src/core/Dates.h
    src/core/Cabrillo.h
    src/core/Callbook.cpp
    src/core/Callbook.h
    src/core/CloudSettings.cpp
    src/core/CloudSettings.h
    src/core/CloudSync.cpp
    src/core/CloudSync.h
    src/core/Countries.cpp
    src/core/Countries.h
    src/core/DecoLinkServer.cpp
    src/core/DecoLinkServer.h
    src/core/ClusterConnection.cpp
    src/core/ClusterConnection.h
    src/core/CredentialStore.cpp
    src/core/CredentialStore.h
    src/core/MailSender.cpp
    src/core/MailSender.h
    src/core/Maidenhead.cpp
    src/core/Modes.cpp
    src/core/Modes.h
    src/core/CwDecoder.cpp
    src/core/CwKeyer.cpp
    src/core/CwKeyer.h
    src/core/Prosistel.cpp
    src/core/Prosistel.h
    src/core/RigControl.cpp
    src/core/RotorGateway.cpp
    src/core/RotorGateway.h
    src/core/RigLink.h
    src/core/TciControl.cpp
    src/core/TciControl.h
    src/core/Maidenhead.h
    src/core/LogDatabase.cpp
    src/core/LogDatabase.h
    src/core/Lotw.cpp
    src/core/Lotw.h
    src/app/LogLibrary.cpp
    src/app/LogLibrary.h
    src/core/ContestRules.cpp
    src/core/ContestRules.h
    src/core/Contests.cpp
    src/core/Contests.h
    src/core/NetworkError.h
    src/core/QslCards.cpp
    src/core/QslDesign.cpp
    src/core/QslDesign.h
    src/core/QslCards.h
    src/core/QslUpload.cpp
    src/core/QslUpload.h
    src/core/RotorLink.cpp
    src/core/RotorLink.h
    src/core/SecretVault.cpp
    src/core/SecretVault.h
    src/core/Solar.cpp
    src/core/Solar.h
    src/core/Spots.cpp
    src/core/Spots.h
    src/core/WsjtxProtocol.cpp
    src/core/WsjtxProtocol.h
    src/core/UdpReceiver.cpp
    src/core/UdpReceiver.h
    src/core/Updates.cpp
    src/core/Updates.h
    src/core/VoiceAnnouncer.cpp
    src/core/VoiceAnnouncer.h
)
qt_add_resources(decolog_core "schema"
    PREFIX "/decolog"
    BASE db
    FILES db/schema.sql
)
# Elenco delle entita' DXCC di AD1C (country-files.com), con i numeri ADIF.
qt_add_resources(decolog_core "cty"
    PREFIX "/decolog"
    BASE resources/cty
    FILES resources/cty/cty.csv
)
# La pagina web del gateway del rotore (quella di DecoRotor), servita sulla 8080.
qt_add_resources(decolog_core "decorotor"
    PREFIX "/decorotor"
    BASE resources/decorotor
    FILES resources/decorotor/index.html
)
target_include_directories(decolog_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Qt6::Gui serve alle etichette QSL: QPdfWriter e QPainter stanno li' dentro, e
# cosi' il PDF si scrive senza moduli di stampa e senza una stampante.
# Qt6::WebSockets: DecoRotor pubblica lo stato del rotore su WebSocket.
# Qt6::SerialPort: il manipolatore CW alza e abbassa DTR o RTS di una porta,
# che e' come si manipola da sempre quando il CAT sta occupato altrove.
target_link_libraries(decolog_core PUBLIC Qt6::Core Qt6::Network Qt6::Sql Qt6::Gui Qt6::WebSockets Qt6::SerialPort)
# Il decodificatore CW: ggmorse fa il lavoro, CwDecoder gli porta l'audio.
target_link_libraries(decolog_core PRIVATE ggmorse)
# Annunci vocali: su Windows la sintesi del sistema (SAPI).
if(WIN32)
    target_link_libraries(decolog_core PRIVATE sapi ole32)
endif()

# Portachiavi di sistema per password e chiavi API. Senza qtkeychain DecoDXLog si
# compila lo stesso, ma non salva segreti: niente ripiego in chiaro.
#   MSYS2:  pacman -S mingw-w64-x86_64-qtkeychain
#   Debian: apt install qtkeychain-qt6-dev
find_package(Qt6Keychain QUIET)
if(TARGET qt6keychain)
    target_link_libraries(decolog_core PUBLIC qt6keychain)
    target_compile_definitions(decolog_core PUBLIC DECODXLOG_HAVE_KEYCHAIN=1)
    message(STATUS "DecoDXLog: qtkeychain found — credentials in the system keystore")
else()
    message(WARNING "DecoDXLog: qtkeychain not found — credentials cannot be stored")
endif()

# Le credenziali che passano dal Cloud viaggiano chiuse, e si chiudono con
# OpenSSL (AES-256-GCM, chiave da PBKDF2): il server non le deve poter leggere.
# Senza OpenSSL DecoDXLog si compila lo stesso e le credenziali restano dove sono.
find_package(OpenSSL QUIET COMPONENTS Crypto)
if(TARGET OpenSSL::Crypto)
    target_link_libraries(decolog_core PUBLIC OpenSSL::Crypto)
    target_compile_definitions(decolog_core PUBLIC DECODXLOG_HAS_OPENSSL=1)
    message(STATUS "DecoDXLog: OpenSSL found — credentials travel sealed")
else()
    message(WARNING "DecoDXLog: OpenSSL not found — credentials stay on this machine")
endif()
if(MSVC)
    target_compile_options(decolog_core PRIVATE /W4)
else()
    target_compile_options(decolog_core PRIVATE -Wall -Wextra)
endif()

# ── Application ───────────────────────────────────────────────────────────────
# Icona e versione dell'eseguibile su Windows (resources/make_icon.py mette
# insieme le taglie di resources/icon; il .rc prende i numeri dal progetto).
if(WIN32)
    configure_file(resources/decodxlog.rc.in ${CMAKE_CURRENT_BINARY_DIR}/decodxlog.rc @ONLY)
    set(DECODXLOG_WIN_RESOURCES ${CMAKE_CURRENT_BINARY_DIR}/decodxlog.rc)
endif()

qt_add_executable(decolog
    ${DECODXLOG_WIN_RESOURCES}
    src/main.cpp
    src/CrashLog.cpp
    src/CrashLog.h
    src/app/QsoTableModel.cpp
    src/app/QsoTableModel.h
    src/app/DecoLogController.cpp
    src/app/DecoLogController.h
    src/app/StationProfileModel.cpp
    src/app/StationProfileModel.h
    src/app/ClusterController.cpp
    src/app/ClusterController.h
    src/app/CloudController.cpp
    src/app/CloudController.h
    src/app/RigController.cpp
    src/app/RotorController.cpp
    src/app/RotorController.h
    src/app/SolarController.cpp
    src/app/SolarController.h
    src/app/UpdateController.cpp
    src/app/UpdateController.h
    src/app/SpotModel.cpp
    src/app/SpotModel.h
    src/app/ActivationController.cpp
    src/app/ActivationController.h
    src/app/QslCardController.cpp
    src/app/QslCardController.h
    src/app/QslController.cpp
    src/app/QslController.h
)

set(DECODXLOG_QML_FILES
    qml/DecoDXLog/Main.qml
    qml/DecoDXLog/TopBar.qml
    qml/DecoDXLog/VfoDisplay.qml
    qml/DecoDXLog/NewQsoPanel.qml
    qml/DecoDXLog/LogbookPanel.qml
    qml/DecoDXLog/BandModeGrid.qml
    qml/DecoDXLog/CallInfoPanel.qml
    qml/DecoDXLog/Ft2AwardPanel.qml
    qml/DecoDXLog/BottomTabs.qml
    qml/DecoDXLog/MapPanel.qml
    qml/DecoDXLog/StatusRail.qml
    qml/DecoDXLog/NewQsoDialog.qml
    qml/DecoDXLog/QslCardDesign.qml
    qml/DecoDXLog/QslCardsWindow.qml
    qml/DecoDXLog/QsoDetailDialog.qml
    qml/DecoDXLog/StationProfilesDialog.qml
    qml/DecoDXLog/SetupDialog.qml
    qml/DecoDXLog/LogbookWindow.qml
    qml/DecoDXLog/CwPanel.qml
    qml/DecoDXLog/OnScreen.qml
    qml/DecoDXLog/PanelWindow.qml
    qml/DecoDXLog/CredentialsList.qml
    qml/DecoDXLog/AwardsDialog.qml
    qml/DecoDXLog/GridSquareMap.qml
    qml/DecoDXLog/AboutDialog.qml
    qml/DecoDXLog/UpdateDialog.qml
    qml/DecoDXLog/ActivationDialog.qml
    qml/DecoDXLog/LogsDialog.qml
    qml/DecoDXLog/ContestLayout.qml
    qml/DecoDXLog/ColumnsDialog.qml
    qml/DecoDXLog/ContestSubmitDialog.qml
    qml/DecoDXLog/ContestEntryPanel.qml
    qml/DecoDXLog/ContestScorePanel.qml
    qml/DecoDXLog/ContestRatePanel.qml
    qml/DecoDXLog/StatsWindow.qml
    qml/DecoDXLog/ContestWindow.qml
    qml/DecoDXLog/RotorPalette.qml
    qml/DecoDXLog/RotorGlass.qml
    qml/DecoDXLog/RotorButton.qml
    qml/DecoDXLog/RotorField.qml
    qml/DecoDXLog/RotorLed.qml
    qml/DecoDXLog/RotorReadout.qml
    qml/DecoDXLog/RotorSense.qml
    qml/DecoDXLog/RotorDisplay.qml
    qml/DecoDXLog/RotorMemoryGrid.qml
    qml/DecoDXLog/RotorCommandBar.qml
    qml/DecoDXLog/RotorPointing.qml
    qml/DecoDXLog/RotorMemories.qml
    qml/DecoDXLog/RotorTopBar.qml
    qml/DecoDXLog/RotorStatus.qml
    qml/DecoDXLog/RotorLocatorBar.qml
    qml/DecoDXLog/RotorMap.qml
    qml/DecoDXLog/RotorDial.qml
    qml/DecoDXLog/RotorControlPage.qml
    qml/DecoDXLog/RotorDiagnostics.qml
    qml/DecoDXLog/RotorSettings.qml
    qml/DecoDXLog/RotorSettingsPanel.qml
    qml/DecoDXLog/RotorTraffic.qml
    qml/DecoDXLog/RotorHistory.qml
    qml/DecoDXLog/RotorStats.qml
    qml/DecoDXLog/RotorNetwork.qml
    qml/DecoDXLog/RotorWindow.qml
    qml/DecoDXLog/RotorPanel.qml
    qml/DecoDXLog/PropagationPanel.qml
    qml/DecoDXLog/ClusterPanel.qml
    qml/DecoDXLog/ClusterWindow.qml
    qml/DecoDXLog/SpotFilterEditor.qml
)

# OUTPUT_DIRECTORY non e' un vezzo: senza, Qt si fa una cartella "DecoDXLog"
# nella cartella di compilazione, e su Linux l'eseguibile si chiama DecoDXLog
# pure lui — il collegamento si ferma con "cannot open output file DecoDXLog:
# Is a directory". Su Windows non succedeva perche' li' l'eseguibile ha .exe.
# Il modulo finisce comunque nelle risorse, quindi spostarlo non cambia niente
# a chi lo usa.
qt_add_qml_module(decolog
    URI DecoDXLog
    VERSION 1.0
    QML_FILES ${DECODXLOG_QML_FILES}
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml_modules/DecoDXLog
)

# Traduzioni: lrelease le compila e le mette fra le risorse (:/i18n). lupdate si
# lancia a mano quando cambiano le stringhe:
#   lupdate -locations none -no-obsolete -recursive src qml libs/decodium-ui \
#           -ts translations/decodxlog_*.ts
# Le quindici lingue sono le stesse di Decodium 4: chi passa da un programma
# all'altro trova la sua. L'inglese e' la lingua in cui sono scritte le
# stringhe; il suo file serve solo a chi vuole correggere l'inglese stesso.
# Per sapere quanto manca:  python scripts/translations.py stato
qt_add_translations(decolog TS_FILES
    translations/decodxlog_it.ts
    translations/decodxlog_en.ts
    translations/decodxlog_de.ts
    translations/decodxlog_fr.ts
    translations/decodxlog_es.ts
    translations/decodxlog_ca.ts
    translations/decodxlog_nl.ts
    translations/decodxlog_da.ts
    translations/decodxlog_hu.ts
    translations/decodxlog_ro.ts
    translations/decodxlog_lv.ts
    translations/decodxlog_ru.ts
    translations/decodxlog_ja.ts
    translations/decodxlog_zh.ts
    translations/decodxlog_zh_TW.ts
)

# L'icona della finestra (la barra delle applicazioni su Windows usa quella del
# .rc, ma la finestra e i dialoghi prendono questa).
# Le coste della mappa: Natural Earth, pubblico dominio (resources/map/COPYRIGHT.txt).
qt_add_resources(decolog "map"
    PREFIX "/decolog"
    BASE resources
    FILES resources/map/coastline.json resources/map/land.json
)

qt_add_resources(decolog "icon"
    PREFIX "/decolog"
    BASE resources
    FILES resources/decodxlog.png
)

# Come in DecoRTTY: il bytecode QML precompilato perde il contesto delle
# traduzioni, e un'interfaccia mezza tradotta vale meno di qualche millisecondo
# all'avvio.
foreach(file IN LISTS DECODXLOG_QML_FILES)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias} QT_QML_SKIP_CACHEGEN TRUE)
endforeach()

target_compile_definitions(decolog PRIVATE DECODXLOG_VERSION="${PROJECT_VERSION}")
target_include_directories(decolog PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(decolog PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Positioning Qt6::Location
    decolog_core
    decodium_ui
    decodium_uiplugin
)
set_target_properties(decolog PROPERTIES
    # Il nome del file e' quello del programma; il bersaglio CMake resta
    # "decolog" perche' e' scritto in mezzo mondo qui dentro e non si vede.
    OUTPUT_NAME DecoDXLog
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.decodium.DecoDXLog"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(DECODXLOG_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(name adif protocol database countries credentials callbook awards decolink lotw spots cluster qsl cabrillo solar rotor rig modes updates cw cwkeyer cloud cloudsettings vault mailsender dates tci rotorgateway)
        qt_add_executable(tst_${name} tests/tst_${name}.cpp)
        target_link_libraries(tst_${name} PRIVATE decolog_core Qt6::Test)
        set_target_properties(tst_${name} PROPERTIES WIN32_EXECUTABLE OFF)
        add_test(NAME ${name} COMMAND tst_${name})
    endforeach()
    # Le etichette QSL disegnano davvero un PDF: serve una QGuiApplication, e
    # senza schermo va bene la piattaforma offscreen.
    qt_add_executable(tst_qslcards tests/tst_qslcards.cpp)
    target_link_libraries(tst_qslcards PRIVATE decolog_core Qt6::Test Qt6::Gui)
    set_target_properties(tst_qslcards PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME qslcards COMMAND tst_qslcards)
    set_tests_properties(qslcards PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # La cartolina QSL disegna sul serio: stessa storia, serve QtGui.
    qt_add_executable(tst_qsldesign tests/tst_qsldesign.cpp)
    target_link_libraries(tst_qsldesign PRIVATE decolog_core Qt6::Test Qt6::Gui)
    set_target_properties(tst_qsldesign PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME qsldesign COMMAND tst_qsldesign)
    set_tests_properties(qsldesign PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # I campi di serie della cartolina stanno nel controller: la prova guarda che
    # cadano dentro i riquadri della QSL, e che il pulsante non li sdoppi.
    qt_add_executable(tst_qslcardcontroller tests/tst_qslcardcontroller.cpp
        src/app/QslCardController.cpp src/app/QslCardController.h)
    target_include_directories(tst_qslcardcontroller PRIVATE src)
    target_link_libraries(tst_qslcardcontroller PRIVATE decolog_core Qt6::Test Qt6::Gui)
    set_target_properties(tst_qslcardcontroller PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME qslcardcontroller COMMAND tst_qslcardcontroller)
    set_tests_properties(qslcardcontroller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # La tabella del log sta nell'applicazione, ma i suoi filtri sono SQL da provare.
    qt_add_executable(tst_qsomodel tests/tst_qsomodel.cpp src/app/QsoTableModel.cpp src/app/QsoTableModel.h)
    target_include_directories(tst_qsomodel PRIVATE src)
    target_link_libraries(tst_qsomodel PRIVATE decolog_core Qt6::Test)
    set_target_properties(tst_qsomodel PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME qsomodel COMMAND tst_qsomodel)
    qt_add_executable(tst_clustercontroller tests/tst_clustercontroller.cpp
        src/app/ClusterController.cpp src/app/ClusterController.h src/app/SpotModel.cpp src/app/SpotModel.h)
    target_include_directories(tst_clustercontroller PRIVATE src)
    target_link_libraries(tst_clustercontroller PRIVATE decolog_core Qt6::Test)
    set_target_properties(tst_clustercontroller PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME clustercontroller COMMAND tst_clustercontroller)
    qt_add_executable(tst_activation tests/tst_activation.cpp
        src/app/ActivationController.cpp src/app/ActivationController.h)
    target_include_directories(tst_activation PRIVATE src)
    target_link_libraries(tst_activation PRIVATE decolog_core Qt6::Test)
    set_target_properties(tst_activation PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME activation COMMAND tst_activation)
    # I log della stazione: crearne uno per il contest e ritrovarli tutti.
    qt_add_executable(tst_loglibrary tests/tst_loglibrary.cpp
        src/app/LogLibrary.cpp src/app/LogLibrary.h)
    target_include_directories(tst_loglibrary PRIVATE src)
    target_link_libraries(tst_loglibrary PRIVATE decolog_core Qt6::Test)
    set_target_properties(tst_loglibrary PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME loglibrary COMMAND tst_loglibrary)
    # Le regole dei contest: ogni numero viene da un regolamento.
    qt_add_executable(tst_contestrules tests/tst_contestrules.cpp)
    target_include_directories(tst_contestrules PRIVATE src)
    target_link_libraries(tst_contestrules PRIVATE decolog_core Qt6::Test)
    set_target_properties(tst_contestrules PROPERTIES WIN32_EXECUTABLE OFF)
    add_test(NAME contestrules COMMAND tst_contestrules)
    set_tests_properties(loglibrary PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Su Windows Qt scrive i suoi messaggi nel canale del debugger quando non
    # trova una console: ctest vede la prova rossa e nessuna riga che dica
    # perche'. Con questo il resoconto esce su stderr e si legge, sul CI come
    # sul proprio computer. Si aggiunge in coda a quello che c'e' gia', perche'
    # qualche prova ha bisogno anche di QT_QPA_PLATFORM.
    get_property(decolog_tests DIRECTORY PROPERTY TESTS)
    foreach(decolog_test IN LISTS decolog_tests)
        set_property(TEST ${decolog_test} APPEND PROPERTY ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    endforeach()
endif()

# ── Tools ─────────────────────────────────────────────────────────────────────
if(DECODXLOG_BUILD_TOOLS)
    # Finge di essere Decodium: manda un QSO in LoggedADIF (e/o QSOLogged) sulla
    # porta UDP. Serve a provare la catena ricezione → SQLite → tabella senza
    # radio e senza aprire Decodium.
    qt_add_executable(decodxlog_udpsend tools/udpsend.cpp)
    target_link_libraries(decodxlog_udpsend PRIVATE decolog_core)
    set_target_properties(decodxlog_udpsend PROPERTIES WIN32_EXECUTABLE OFF)
    # Si collega a un nodo, RBN o POTA e stampa gli spot: per provare una fonte.
    qt_add_executable(decodxlog_clusterprobe tools/clusterprobe.cpp)
    target_link_libraries(decodxlog_clusterprobe PRIVATE decolog_core)
    set_target_properties(decodxlog_clusterprobe PROPERTIES WIN32_EXECUTABLE OFF)
endif()

install(TARGETS decolog
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)
