import QtQuick

Loader {
    id: launcher
    active: false
    asynchronous: true
    source: "QsoJourney.qml"
    property var detail

    function openJourney(nextDetail) {
        detail = nextDetail
        if (item)
            item.present(detail)
        else
            active = true
    }

    onLoaded: item.present(detail)
    Connections {
        target: launcher.item
        function onClosing() {
            Qt.callLater(function() {
                if (launcher.item && !launcher.item.visible)
                    launcher.active = false
            })
        }
    }
}
