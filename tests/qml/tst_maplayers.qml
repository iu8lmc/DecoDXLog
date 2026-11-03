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
    QtObject {
        id: fakeDecolog
        property var callInfo: ({position: null, call: "", gridsquare: ""})
        property var myPosition: null
        property var gridPoints: [{lat: 10, lon: 20}]
        property QtObject rotor: QtObject { property bool enabled: true; property var state: ({connected: true, az: 90}) }
        property QtObject solar: QtObject { property var data: ({valid: false}) }
        property QtObject cluster: QtObject {
            property QtObject spots: QtObject { signal countChanged() }
            function mapSpots() { return [{lat: 20, lon: 30}] }
        }
        function coastline() { return [] }
        function landmasses() { return [] }
        signal logChanged()
        signal lookupChanged()
        signal stationChanged()
    }
    Component { id: mapComponent; MapPanel { width: 600; height: 360 } }
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
