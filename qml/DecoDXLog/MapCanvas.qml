// Il renderer completo della mappa. Viene caricato solo sulle piattaforme che
// possono usare Canvas senza mandare in crisi il scene graph.
import QtQuick
import Decodium.UI

Rectangle {
    id: root

    property bool showCoast: true
    property bool showNight: true
    property real nightOpacity: 0.30
    property bool showGrids: true
    property bool showSpots: true
    property bool showRotor: true
    property var target: null
    property var home: null
    property var grids: []
    property var spots: []
    property var coastline: []

    // Se qualcuno chiede esplicitamente Canvas su Linux, resta comunque sul
    // target immagine: evita il FBO, che e' la parte piu' fragile dei vecchi
    // driver Mesa. In modalita' normale Linux non carica proprio questo file.
    readonly property bool cpuCanvas: Qt.platform.os === "linux"

    radius: 4
    color: Theme.bgMedium
    border.width: 1
    border.color: Theme.borderSoft
    clip: true

    function repaintAll() {
        background.requestPaint()
        overlay.requestPaint()
    }
    function repaintBackground() { background.requestPaint() }
    function repaintOverlay() { overlay.requestPaint() }

    Canvas {
        id: background
        anchors.fill: parent
        anchors.margins: 1
        renderTarget: root.cpuCanvas ? Canvas.Image : Canvas.FramebufferObject
        renderStrategy: root.cpuCanvas ? Canvas.Immediate : Canvas.Cooperative
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function px(lon) { return (lon + 180) / 360 * width }
        function py(lat) { return (90 - lat) / 180 * height }

        function sunPosition(now) {
            const day = now.getTime() / 86400000 + 2440587.5 - 2451545.0
            const meanLongitude = (280.46 + 0.9856474 * day) % 360
            const meanAnomaly = ((357.528 + 0.9856003 * day) % 360) * Math.PI / 180
            const lambda = (meanLongitude + 1.915 * Math.sin(meanAnomaly)
                            + 0.02 * Math.sin(2 * meanAnomaly)) * Math.PI / 180
            const obliquity = 23.439 * Math.PI / 180
            const declination = Math.asin(Math.sin(obliquity) * Math.sin(lambda))
            const utcHours = now.getUTCHours() + now.getUTCMinutes() / 60 + now.getUTCSeconds() / 3600
            return { declination: declination, lon: 180 - utcHours * 15 }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            if (root.showNight) {
                const sun = sunPosition(new Date())
                ctx.fillStyle = Qt.rgba(0, 0, 0, Math.max(0, Math.min(1, root.nightOpacity)))
                const step = 2
                for (let x = 0; x < width; x += step) {
                    const lon = x / width * 360 - 180
                    const hourAngle = (lon - sun.lon) * Math.PI / 180
                    const t = -Math.cos(hourAngle) / Math.tan(sun.declination)
                    const lat = Math.atan(t) * 180 / Math.PI
                    if (sun.declination > 0)
                        ctx.fillRect(x, py(lat), step, height - py(lat))
                    else
                        ctx.fillRect(x, 0, step, py(lat))
                }
            }

            if (root.showCoast && root.coastline.length > 0) {
                ctx.strokeStyle = Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g,
                                          Theme.textSecondary.b, 0.55)
                ctx.lineWidth = 1
                for (const line of root.coastline) {
                    ctx.beginPath()
                    for (let i = 0; i < line.length; ++i) {
                        const x = px(line[i][0])
                        const y = py(line[i][1])
                        if (i === 0)
                            ctx.moveTo(x, y)
                        else
                            ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }
            }

            ctx.lineWidth = 1
            ctx.strokeStyle = Qt.rgba(Theme.borderSoft.r, Theme.borderSoft.g,
                                      Theme.borderSoft.b, 0.6)
            for (let lon = -150; lon < 180; lon += 30) {
                ctx.beginPath()
                ctx.moveTo(px(lon), 0)
                ctx.lineTo(px(lon), height)
                ctx.stroke()
            }
            for (let lat = -60; lat <= 60; lat += 30) {
                ctx.strokeStyle = lat === 0 ? Theme.glassBorder
                                            : Qt.rgba(Theme.borderSoft.r, Theme.borderSoft.g,
                                                      Theme.borderSoft.b, 0.6)
                ctx.beginPath()
                ctx.moveTo(0, py(lat))
                ctx.lineTo(width, py(lat))
                ctx.stroke()
            }

            if (root.showGrids) {
                ctx.fillStyle = Qt.rgba(Theme.secondaryColor.r, Theme.secondaryColor.g,
                                        Theme.secondaryColor.b, 0.75)
                for (const point of root.grids)
                    ctx.fillRect(px(point.lon) - 1, py(point.lat) - 1, 2.5, 2.5)
            }
        }
    }

    Canvas {
        id: overlay
        anchors.fill: parent
        anchors.margins: 1
        renderTarget: root.cpuCanvas ? Canvas.Image : Canvas.FramebufferObject
        renderStrategy: root.cpuCanvas ? Canvas.Immediate : Canvas.Cooperative

        function px(lon) { return (lon + 180) / 360 * width }
        function py(lat) { return (90 - lat) / 180 * height }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            if (root.showSpots) {
                for (const spot of root.spots) {
                    const x = px(spot.lon)
                    const y = py(spot.lat)
                    const isNew = (spot.status & 1) !== 0
                    const isBandOrMode = (spot.status & 6) !== 0
                    ctx.fillStyle = isNew ? Theme.errorColor
                                           : isBandOrMode ? Theme.warningColor : Theme.accentColor
                    ctx.globalAlpha = isNew || isBandOrMode ? 0.95 : 0.6
                    ctx.beginPath()
                    ctx.arc(x, y, isNew ? 4 : 3, 0, 2 * Math.PI)
                    ctx.fill()
                    if (isNew) {
                        ctx.globalAlpha = 0.5
                        ctx.strokeStyle = Theme.errorColor
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        ctx.arc(x, y, 8, 0, 2 * Math.PI)
                        ctx.stroke()
                    }
                    ctx.globalAlpha = 1
                }
            }

            if (root.target && root.target.lat !== undefined) {
                ctx.fillStyle = Theme.accentColor
                ctx.beginPath()
                ctx.arc(px(root.target.lon), py(root.target.lat), 4, 0, 2 * Math.PI)
                ctx.fill()
            }

            if (root.home && root.home.lat !== undefined) {
                const hx = px(root.home.lon)
                const hy = py(root.home.lat)
                if (root.target && root.target.lat !== undefined) {
                    const toRad = Math.PI / 180
                    const lat1 = root.home.lat * toRad
                    const lon1 = root.home.lon * toRad
                    const lat2 = root.target.lat * toRad
                    const lon2 = root.target.lon * toRad
                    const d = 2 * Math.asin(Math.sqrt(Math.pow(Math.sin((lat2 - lat1) / 2), 2)
                              + Math.cos(lat1) * Math.cos(lat2)
                              * Math.pow(Math.sin((lon2 - lon1) / 2), 2)))
                    ctx.strokeStyle = Theme.accentColor
                    ctx.lineWidth = 1.5
                    ctx.globalAlpha = 0.9
                    ctx.beginPath()
                    let previousX = null
                    for (let i = 0; i <= 64; ++i) {
                        const f = i / 64
                        const a = Math.sin((1 - f) * d) / Math.sin(d)
                        const b = Math.sin(f * d) / Math.sin(d)
                        const x = a * Math.cos(lat1) * Math.cos(lon1)
                                  + b * Math.cos(lat2) * Math.cos(lon2)
                        const y = a * Math.cos(lat1) * Math.sin(lon1)
                                  + b * Math.cos(lat2) * Math.sin(lon2)
                        const z = a * Math.sin(lat1) + b * Math.sin(lat2)
                        const lat = Math.atan2(z, Math.sqrt(x * x + y * y)) / toRad
                        const lon = Math.atan2(y, x) / toRad
                        const cx = px(lon)
                        const cy = py(lat)
                        if (i === 0 || (previousX !== null && Math.abs(cx - previousX) > width / 2))
                            ctx.moveTo(cx, cy)
                        else
                            ctx.lineTo(cx, cy)
                        previousX = cx
                    }
                    ctx.stroke()
                    ctx.globalAlpha = 1
                }

                const rotor = decolog.rotor.state
                if (rotor.connected === true && root.showRotor) {
                    const heading = (rotor.az || 0) * Math.PI / 180
                    const arm = Math.min(width, height) * 0.22
                    ctx.strokeStyle = Theme.warningColor
                    ctx.lineWidth = 2
                    ctx.globalAlpha = 0.85
                    ctx.beginPath()
                    ctx.moveTo(hx, hy)
                    ctx.lineTo(hx + arm * Math.sin(heading), hy - arm * Math.cos(heading))
                    ctx.stroke()
                    ctx.globalAlpha = 1
                }

                ctx.fillStyle = Theme.primaryColor
                ctx.beginPath()
                ctx.arc(hx, hy, 4, 0, 2 * Math.PI)
                ctx.fill()
                ctx.strokeStyle = Theme.primaryColor
                ctx.lineWidth = 1.5
                ctx.beginPath()
                ctx.arc(hx, hy, 7, 0, 2 * Math.PI)
                ctx.stroke()
            }
        }
    }
}
