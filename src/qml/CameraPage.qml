import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Camera: a mirror, and a photo booth with templates and filters. The page itself is only this shell: the camera (QtMultimedia) lives in
// CameraView.qml, which is loaded when the page is opened and let go of when it closes, so nothing of Qt Multimedia is in memory or running otherwise.
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "camera"
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 8

    Loader {
        id: view
        Layout.fillWidth: true
        Layout.preferredHeight: item ? item.implicitHeight : 80
        active: page.visible
        source: "CameraView.qml"
        onLoaded: item.shell = page.shell
    }
    Text {
        visible: view.status === Loader.Error
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: theme.textDim
        font.pixelSize: 12
        text: "The camera needs Qt's multimedia module for QML (Debian and Ubuntu: qml6-module-qtmultimedia). Install it and open this page again."
    }
}
