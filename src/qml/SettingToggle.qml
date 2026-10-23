import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One settings row: title + optional description on the left, switch pinned to the right edge.
RowLayout {
    id: st
    property string title
    property string caption
    property bool checked
    signal toggled()
    Layout.fillWidth: true
    spacing: 16
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        SectionLabel {
            Layout.fillWidth: true
            text: st.title
        }
        Caption {
            Layout.fillWidth: true
            visible: st.caption !== ""
            text: st.caption
        }
    }
    Toggle {
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        checked: st.checked
        onClicked: st.toggled()
        Accessible.name: st.title
    }
}
