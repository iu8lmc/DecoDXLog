import QtQuick
import QtTest
import "../../qml/DecoDXLog"

TestCase {
    name: "QsoJourney"
    when: windowShown
    property var decolog: ({stationProfiles: {byId: function() { return {} }}})
    Component { id: component; QsoJourney {} }
    function verifyGround(window) {
        const frame = grabImage(window.contentItem)
        let white = 0
        let min = 255
        let max = 0
        for (let ix = 1; ix <= 9; ++ix) {
            for (let iy = 2; iy <= 8; ++iy) {
                const x = Math.floor(frame.width * ix / 10)
                const y = Math.floor(frame.height * iy / 10)
                const red = frame.red(x, y)
                min = Math.min(min, red)
                max = Math.max(max, red)
                if (red > 245 && frame.green(x,y) > 245 && frame.blue(x,y) > 245) ++white
            }
        }
        verify(white < 4 && max - min > 10, "Expected visible terrain beneath streamed tiles")
    }
    function test_replay_data() {
        return [{tag:"A_missing", grid:"", valid:false}, {tag:"B_Portugal_Malta", grid:"JM75fv", valid:true},
                {tag:"C_Portugal_Malta_no_imagery", grid:"JM75fv", valid:true, offline:true}]
    }
    function test_replay(data) {
        const view = createTemporaryObject(component, null)
        verify(view)
        view.present({fields:{MY_GRIDSQUARE:"IN60ap", GRIDSQUARE:data.grid}})
        if (data.offline) findChild(view, "streamedImagery").active = false
        compare(view.ready, data.valid)
        if (data.valid) {
            verify(view.distance > 1900000 && view.distance < 2300000)
            tryVerify(function() { return view.progress > 0 }, 8000)
            const map = findChild(view, "journeyMap")
            verify(map.tilt > 0)
            if (data.offline) verifyGround(view)
            const initial = view.origin.distanceTo(map.center)
            tryVerify(function() { return view.origin.distanceTo(map.center) > initial + 100 }, 4000)
            tryVerify(function() { return view.reveal === 1 }, 22000)
            if (data.offline) verifyGround(view)
            compare(map.tilt, 0)
            verify(map.center.distanceTo(view.overviewCenter) < 10)
        } else compare(view.progress, 0)
        view.close()
    }
}
