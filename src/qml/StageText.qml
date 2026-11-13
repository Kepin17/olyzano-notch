import QtQuick

// A line of text that changes with a short fade and slide, so a new stage ("Cleaning the sound" → "Writing it down") is noticed instead of jumping.
Item {
    id: st
    property string text
    property color color: theme.text
    property int pixelSize: 12
    property int weight: Font.Normal
    property string shown: ""
    implicitHeight: label.implicitHeight
    clip: true

    onTextChanged: {
        if (!activities.animations || shown === "" || shown === text) { shown = text; return }
        swap.restart()
    }
    Component.onCompleted: shown = text
    Text {
        id: label
        width: parent.width
        text: st.shown
        textFormat: Text.PlainText
        color: st.color
        font.pixelSize: st.pixelSize
        font.weight: st.weight
        elide: Text.ElideRight
    }
    SequentialAnimation {
        id: swap
        ParallelAnimation {
            NumberAnimation { target: label; property: "opacity"; to: 0; duration: 110 }
            NumberAnimation { target: label; property: "y"; to: -6; duration: 110; easing.type: Easing.InQuad }
        }
        ScriptAction { script: { st.shown = st.text; label.y = 7 } }
        ParallelAnimation {
            NumberAnimation { target: label; property: "opacity"; to: 1; duration: 200 }
            NumberAnimation { target: label; property: "y"; to: 0; duration: 220; easing.type: Easing.OutCubic }
        }
    }
}
