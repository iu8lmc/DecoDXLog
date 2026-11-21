import QtQuick
import QtCore
import QtQuick.Controls
import QtLocation
import QtPositioning

Window {
    id: view
    title: qsTr("QSO map")
    property var fields: ({})
    property bool preview: false
    property bool approximateDestination: false
    property var origin: QtPositioning.coordinate()
    property var destination: QtPositioning.coordinate()
    property real progress: 0
    readonly property bool ready: origin.isValid && destination.isValid
    readonly property real distance: ready ? origin.distanceTo(destination) : 0
    readonly property real bearing: ready ? origin.azimuthTo(destination) : 0

    function degrees(value) {
        const s = String(value ?? "").trim()
        const m = /^([NSEW])\s*(\d+)\s+(\d+(?:\.\d+)?)$/i.exec(s)
        if (m && Number(m[3]) < 60)
            return (Number(m[2]) + Number(m[3])/60) * (/[SW]/i.test(m[1]) ? -1 : 1)
        return s.length ? Number(s) : NaN
    }
    function position(lat, lon, locator) {
        const coord = QtPositioning.coordinate(degrees(lat), degrees(lon))
        if (coord.isValid) return coord
        const g = String(locator || "").trim().toUpperCase()
        if (!/^[A-R]{2}[0-9]{2}([A-X]{2}([0-9]{2})?)?$/.test(g))
            return QtPositioning.coordinate()
        let x = -180 + 20*(g.charCodeAt(0)-65) + 2*Number(g[2])
        let y = -90 + 10*(g.charCodeAt(1)-65) + Number(g[3])
        let step = 1
        if (g.length >= 6) {
            step /= 24
            x += 2*step*(g.charCodeAt(4)-65)
            y += step*(g.charCodeAt(5)-65)
        }
        if (g.length === 8) {
            step /= 10
            x += 2*step*Number(g[6])
            y += step*Number(g[7])
        }
        return QtPositioning.coordinate(y+step/2,x+step)
    }
    function present(detail) {
        fields = detail.fields || {}
        const routePreview = detail.preview
        preview = !!routePreview
        approximateDestination = preview && !!routePreview.approximate
        if (preview) {
            origin = QtPositioning.coordinate(routePreview.origin.lat, routePreview.origin.lon)
            destination = QtPositioning.coordinate(routePreview.destination.lat, routePreview.destination.lon)
        } else {
            const profile = decolog.stationProfiles.byId(detail.stationProfileId || 0) || {}
            origin = position(fields.MY_LAT, fields.MY_LON, fields.MY_GRIDSQUARE || profile.myGridsquare)
            destination = position(fields.LAT, fields.LON, fields.GRIDSQUARE)
        }
        showMaximized()
        raise()
        Qt.callLater(frame)
    }
    readonly property string cachePath: decodeURIComponent(StandardPaths.writableLocation(StandardPaths.CacheLocation).toString().replace(Qt.platform.os === "windows" ? "file:///" : "file://", ""))
    property bool satellite: true
    property var overviewCenter
    property real reveal: 0
    function revealOverview() {
        if (!overviewCenter || !ready) return
        map.center = destination.atDistanceAndAzimuth(destination.distanceTo(overviewCenter)*reveal,
                                                       destination.azimuthTo(overviewCenter))
        map.zoomLevel = overviewZoom + (Math.min(15, overviewZoom+4)-overviewZoom)*(1-reveal)
        map.tilt = 45*(1-reveal)
        map.bearing = ((arrivalBearing+540)%360-180)*(1-reveal)
    }
    property real arrivalBearing: 0
    onRevealChanged: revealOverview()
    property real overviewZoom: 5
    function updateCamera() {
        if (!ready || !map.mapReady) return
        const t = progress
        const point = origin.atDistanceAndAzimuth(distance*t, bearing)
        map.center = point
        map.zoomLevel = Math.min(15, overviewZoom + 1.3 + 2.7*Math.pow(2*t-1, 2))
        map.tilt = 45 + 12*Math.sin(Math.PI*t)
        if (distance > 1) {
            const behind = origin.atDistanceAndAzimuth(distance*Math.max(0,t-0.001), bearing)
            map.bearing = t < 0.001 ? bearing : behind.azimuthTo(point)
        }
    }
    onProgressChanged: updateCamera()
    function frame() {
        if (!ready || !map.mapReady) return
        map.tilt = 0
        map.bearing = 0
        map.fitViewportToMapItems([route])
        overviewCenter = map.center
        overviewZoom = Math.min(12, map.zoomLevel - 0.4)
        updateCamera()
        flight.restart()
    }
    Plugin {
        id: labels
        name: "osm"
        PluginParameter { name: "osm.useragent"; value: "DecoDXLog QSO map" }
        PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
        PluginParameter { name: "osm.mapping.custom.host"; value: "https://services.arcgisonline.com/ArcGIS/rest/services/Reference/World_Boundaries_and_Places/MapServer/tile/%z/%y/%x" }
        PluginParameter { name: "osm.mapping.cache.directory"; value: view.cachePath + "/journey-labels" }
        PluginParameter { name: "osm.mapping.custom.mapcopyright"; value: "Imagery and labels © Esri and contributors" }
    }
    // Local Web Mercator imagery remains beneath the streamed detail at every frame.
    Map {
        anchors.fill: parent
        plugin: Plugin { name: "itemsoverlay" }
        color: "#18374a"
        copyrightsVisible: false
        center: map.center
        zoomLevel: map.zoomLevel
        tilt: map.tilt
        bearing: map.bearing
        MapItemView {
            model: 4
            delegate: MapQuickItem {
                required property int index
                coordinate: QtPositioning.coordinate(index < 2 ? 66.51326044 : -66.51326044,
                                                     index % 2 ? 90 : -90)
                zoomLevel: 3
                anchorPoint.x: 512
                anchorPoint.y: 512
                sourceItem: Image {
                    width: 1024; height: 1024
                    source: "journey/earth-" + index + ".jpg"
                    mipmap: true
                }
            }
        }
    }
    Loader {
        objectName: "streamedImagery"
        id: baseTiles
        anchors.fill: parent
        property string host: view.satellite ? "https://services.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%z/%y/%x" : "https://tile.openstreetmap.org/"
        onHostChanged: { active = false; active = true }
        sourceComponent: Component {
            Map {
                color: "transparent"
                plugin: Plugin {
                    name: "osm"
                    PluginParameter { name: "osm.useragent"; value: "DecoDXLog QSO map (github.com/elisir80/DecodxLog)" }
                    PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
                    PluginParameter { name: "osm.mapping.custom.host"; value: baseTiles.host }
                    PluginParameter { name: "osm.mapping.cache.directory"; value: view.cachePath + (view.satellite ? "/journey-imagery" : "/journey-osm") }
                    PluginParameter { name: "osm.mapping.custom.mapcopyright"; value: view.satellite ? "Esri World Imagery" : "© OpenStreetMap contributors" }
                }
                onSupportedMapTypesChanged: { for (const type of supportedMapTypes) { if (type.style === MapType.CustomMap) activeMapType = type } }
                center: map.center
                tilt: map.tilt
                bearing: map.bearing
                zoomLevel: map.zoomLevel
            }
        }
    }
    Map {
        id: map
        objectName: "journeyMap"
        anchors.fill: parent
        plugin: labels
        color: "transparent"
        onSupportedMapTypesChanged: {
            for (const type of supportedMapTypes)
                if (type.style === MapType.CustomMap) activeMapType = type
        }
        onMapReadyChanged: if (mapReady) Qt.callLater(view.frame)
        MapPolyline {
            id: route
            line.width: 4
            line.color: "#26bef7"
            path: {
                if (!view.ready) return []
                const points = [view.origin]
                for (let i = 1; i < 64; ++i)
                    points.push(view.origin.atDistanceAndAzimuth(view.distance*i/64, view.bearing))
                points.push(view.destination)
                return points
            }
        }
        MapQuickItem {
            visible: view.ready
            coordinate: view.destination
            anchorPoint.x: 7
            anchorPoint.y: 7
            sourceItem: Rectangle { width: 14; height: 14; radius: 7; color: "#163950"; border.color: "white"; border.width: 2 }
        }
        MapQuickItem {
            visible: view.ready
            coordinate: view.ready ? view.origin.atDistanceAndAzimuth(view.progress*view.distance,view.bearing) : view.origin
            anchorPoint.x: 10
            anchorPoint.y: 10
            sourceItem: Rectangle { width: 20; height: 20; radius: 10; color: "#ffb142"; border.color: "#162c40"; border.width: 3 }
        }
    }
    SequentialAnimation {
        id: flight
        ScriptAction { script: { view.reveal = 0; view.progress = 0; view.updateCamera() } }
        NumberAnimation { target: view; property: "progress"; from: 0; to: 1; duration: 13000; easing.type: Easing.InOutSine }
        PauseAnimation { duration: 900 }
        ScriptAction { script: view.arrivalBearing = map.bearing }
        NumberAnimation { target: view; property: "reveal"; from: 0; to: 1; duration: 3600; easing.type: Easing.InOutSine }
    }
    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; margins: 24 }
        color: "#ed13293a"; radius: 12
        width: info.implicitWidth + 32; height: info.implicitHeight + 24
        Column {
            id: info
            anchors.centerIn: parent
            spacing: 5
            Label { color: "white"; font.pixelSize: 23; font.bold: true; text: (view.fields.STATION_CALLSIGN || "QTH") + "  →  " + (view.fields.CALL || "") }
            Label {
                color: "#bce7ed"
                text: view.preview ? qsTr("Path preview")
                                   : [view.fields.BAND, view.fields.SUBMODE || view.fields.MODE,
                                      view.fields.QSO_DATE, view.fields.TIME_ON].filter(Boolean).join("  ·  ")
            }
            Label { color: "white"; text: view.ready ? Math.round(view.distance/1000) + " km" : qsTr("Station or contact coordinates missing") }
            Label { visible: view.approximateDestination; color: "#bce7ed"; text: qsTr("Approximate destination (DXCC centre)") }
            Button { text: view.satellite ? "OpenStreetMap" : qsTr("Satellite"); onClicked: view.satellite = !view.satellite }
            Button { visible: view.ready; text: "↻"; Accessible.name: qsTr("Replay"); onClicked: flight.restart() }
        }
    }
    Shortcut { sequence: "Escape"; onActivated: view.close() }
}
