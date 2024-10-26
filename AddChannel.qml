import QtQuick 2.3
import QtQuick.Controls 1.2 as QtControls
import QtQuick.Layouts 1.1
import Oxygen 1.0
import Oxygen.Dialogs 1.0
import Oxygen.Layouts 1.0
import Oxygen.Themes 1.0
import Oxygen.Tools 1.0
import Oxygen.Widgets 1.0


Item
{
    id: root

    property var channels: QObjectTreeModel {}

    property string conn_string: "tcp://localhost:5555"
    readonly property bool settingsValid: conn_string !== ""

    function queryProperties()
    {
        var props = plugin.createPropertyList();

        props.setString("DAQOPEN_ZMQ_SUB/ZmqConnStr", root.conn_string);

        return props;
    }

    ColumnLayout
    {
        anchors.leftMargin: Theme.smallMargin
        anchors.rightMargin: Theme.smallMargin
        anchors.fill: parent

        spacing: Theme.mediumSpacing

        TextField
        {
            id: idInputField
            Layout.fillWidth: true
            text: root.conn_string
            readOnly: false
            placeholderText: qsTranslate("DAQOPEN_ZMQ_SUB/AddChannel", "Connection String")

            onTextChanged: {
                root.conn_string = text;
            }
        }

        VerticalSpacer {}

    }

}
