import QtQuick
import QtTest
import "../../qml/DecoDXLog"

TestCase {
    id: testCase
    name: "MapLayers"
    when: windowShown
    width: 640
    height: 420
    property bool mapUsesSafeRenderer: false
    property var decolog: fakeDecolog
    property var testSpots: [{lat: 20, lon: 30}]
    QtObject {
        id: fakeDecolog
        property var callInfo: ({position: null, call: "", gridsquare: ""})
        property var myPosition: null
        property var gridPoints: [{lat: 10, lon: 20}]
        property QtObject rotor: QtObject { property bool enabled: true; property var state: ({connected: true, az: 90}) }
        property QtObject solar: QtObject { property var data: ({valid: false}) }
        property QtObject cluster: QtObject {
            property QtObject spots: QtObject { signal countChanged() }
            function mapSpots() { return testCase.testSpots }
        }
        function coastline() { return [] }
        function landmasses() { return [] }
        signal logChanged()
        signal lookupChanged()
        signal stationChanged()
    }
    Component { id: mapComponent; MapPanel { width: 600; height: 360 } }
    SignalSpy { id: spotPaint; signalName: "paint" }
    SignalSpy { id: targetPaint; signalName: "paint" }
    function test_lookupDoesNotRedrawClusterSpots() {
        mapUsesSafeRenderer = false
        const many = []
        for (let i = 0; i < 2000; ++i)
            many.push({lat: (i % 160) - 80, lon: (i % 350) - 175, status: i % 7})
        testSpots = many
        const panel = createTemporaryObject(mapComponent, testCase)
        verify(panel !== null)
        const renderer = findChild(panel, "mapRenderer")
        tryVerify(function() { return renderer && renderer.item !== null })
        const spotLayer = findChild(renderer.item, "mapSpotLayer")
        const targetLayer = findChild(renderer.item, "mapTargetLayer")
        verify(spotLayer !== null, "cluster spots have their own canvas")
        verify(targetLayer !== null, "selected callsign has its own canvas")
        spotPaint.target = spotLayer
        targetPaint.target = targetLayer
        panel.reloadSpots()
        tryVerify(function() { return spotPaint.count > 0 })
        wait(100)
        // A: reproduce the previous combined repaint explicitly; B: only the
        // selected-call layer is repainted. Both use the same 2000 spots.
        spotPaint.clear(); targetPaint.clear()
        const baselineStart = Date.now()
        for (let i = 0; i < 5; ++i) {
            renderer.item.repaintSpots()
            fakeDecolog.callInfo = {position: {lat: 45 + i, lon: 4}, call: "ON4ABC", gridsquare: "JO20"}
            fakeDecolog.lookupChanged()
            tryVerify(function() { return spotPaint.count >= i + 1 && targetPaint.count >= i + 1 })
        }
        const baselineMs = Date.now() - baselineStart
        spotPaint.clear(); targetPaint.clear()
        const newStart = Date.now()
        for (let i = 0; i < 5; ++i) {
            fakeDecolog.callInfo = {position: {lat: 51 + i, lon: 4}, call: "ON4ABC", gridsquare: "JO20"}
            fakeDecolog.lookupChanged()
            tryVerify(function() { return targetPaint.count >= i + 1 })
        }
        const newMs = Date.now() - newStart
        wait(100)
        compare(spotPaint.count, 0, "lookup must not repaint the 2000 cluster spots")
        console.log("MAP A/B 2000 spots: old-style 5 spot repaints " + baselineMs
                    + " ms; new 0 spot repaints " + newMs + " ms")
        spotPaint.clear()
        panel.reloadSpots()
        tryVerify(function() { return spotPaint.count > 0 }, 5000,
                  "new cluster spots must still repaint their layer")
        spotPaint.target = null; targetPaint.target = null
        testSpots = [{lat: 20, lon: 30}]
    }
    function test_layers_data() {
        return [{tag: "canvas", safe: false}, {tag: "compatible", safe: true}]
    }
    function test_layers(data) {
        mapUsesSafeRenderer = data.safe
        const panel = createTemporaryObject(mapComponent, testCase)
        verify(panel !== null)
        const renderer = findChild(panel, "mapRenderer")
        tryVerify(function() { return renderer && renderer.item !== null })
        const layers = [
            ["mapLayerCoast", "showCoast"],
            ["mapLayerGrids", "showGrids"],
            ["mapLayerSpots", "showSpots"],
            ["mapLayerRotor", "showRotor"]
        ]
        if (!data.safe) layers.push(["mapLayerNight", "showNight"])
        for (const layer of layers) {
            const item = findChild(panel, layer[0])
            verify(item !== null, layer[0])
            compare(panel[layer[1]], true)
            compare(item.layerVisible, true)
            compare(item.indicator.text, "✓")
            item.triggered()
            compare(panel[layer[1]], false)
            compare(item.layerVisible, false)
            compare(item.indicator.text, "")
            compare(renderer.item[layer[1]], false)
            item.triggered()
            compare(panel[layer[1]], true)
            compare(item.layerVisible, true)
            compare(item.indicator.text, "✓")
            compare(renderer.item[layer[1]], true)
        }
    }
    function test_nightIntensity() {
        mapUsesSafeRenderer = false
        const panel = createTemporaryObject(mapComponent, testCase)
        const renderer = findChild(panel, "mapRenderer")
        tryVerify(function() { return renderer && renderer.item !== null })
        const slider = findChild(panel, "mapNightOpacity")
        verify(slider !== null)
        panel.nightOpacity = 0.30
        compare(renderer.item.nightOpacity, 0.30)
        slider.value = 0.70
        slider.moved()
        compare(panel.nightOpacity, 0.70)
        compare(renderer.item.nightOpacity, 0.70)
        panel.nightOpacity = 0
        compare(renderer.item.nightOpacity, 0)
        panel.nightOpacity = 0.30
    }
}
