import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.foilauth 1.0

import "harbour"
import "foil-ui"

Item {
    id: thisView

    property var deviceAuth
    readonly property bool _canSave: deviceAuth.available && !deviceAuth.enabled
    property bool _authenticationAttempted
    property bool _passwordFallback
    readonly property bool _showPassword: !deviceAuth.enabled || _passwordFallback

    function tryDeviceUnlock() {
        if (!_completed || !deviceAuth.ready || _authenticationAttempted || HarbourSystemState.locked ||
                !foilUi.isLockedState(foilModel.foilState) || page.status !== PageStatus.Active ||
                Qt.application.state !== Qt.ApplicationActive) return
        if (!deviceAuth.enabled || !deviceAuth.available) {
            _passwordFallback = true
            requestFocus()
        } else if (!deviceAuth.busy) {
            requestDeviceUnlock()
        }
    }

    function requestDeviceUnlock() {
        if (!HarbourSystemState.locked && deviceAuth.ready && deviceAuth.available && deviceAuth.enabled && !deviceAuth.busy) {
            _authenticationAttempted = true
            _passwordFallback = false
            inputField.focus = false
            Qt.inputMethod.hide()
            deviceAuth.unlock()
        }
    }

    Connections {
        target: deviceAuth
        onReadyChanged: tryDeviceUnlock()
        onBusyChanged: {
            if (!deviceAuth.busy && _authenticationAttempted &&
                    foilUi.isLockedState(foilModel.foilState)) {
                _passwordFallback = true
                requestFocus()
            }
        }
    }
    Connections {
        target: HarbourSystemState
        onLockedChanged: {
            if (!HarbourSystemState.locked) tryDeviceUnlock()
        }
    }
    Connections {
        target: page
        onStatusChanged: tryDeviceUnlock()
    }
    Connections {
        target: Qt.application
        onStateChanged: tryDeviceUnlock()
    }

    property var foilUi
    property var foilModel
    property Page page
    property Component iconComponent

    property bool _completed
    property bool _wrongPassword
    readonly property var _settings: foilUi.settings
    readonly property bool _landscapeLayout: page.isLandscape && Screen.sizeCategory < Screen.Large
    readonly property int _screenHeight: page.isLandscape ? Screen.width : Screen.height
    readonly property bool _unlocking: !foilUi.isLockedState(foilModel.foilState)
    readonly property bool _canEnterPassword: inputField.text.length > 0 && !_unlocking &&
                                    !deviceAuth.busy && !wrongPasswordAnimation.running && !_wrongPassword

    function enterPassword() {
        if (!foilModel.unlock(inputField.text)) {
            _wrongPassword = true
            wrongPasswordAnimation.start()
            requestFocus()
        }
    }

    function requestFocus() {
        if (_showPassword && !HarbourSystemState.locked) inputField.requestFocus()
    }

    Component.onCompleted: {
        _completed = true
        tryDeviceUnlock()
    }

    Timer {
        id: pullDownMenuVisibleTimer

        interval: 400
        onTriggered: pullDownMenu.visible = true
    }

    PullDownMenu {
        id: pullDownMenu

        visible: false

        readonly property bool shouldBeVisible: _completed && !Qt.inputMethod.visible

        // Hide immediately, show with a delay
        onShouldBeVisibleChanged: {
            if (shouldBeVisible) {
                pullDownMenuVisibleTimer.restart()
            } else {
                pullDownMenuVisibleTimer.stop()
                visible = false
            }
        }

        MenuItem {
            text: foilUi.qsTrEnterPasswordViewMenuGenerateNewKey()
            onClicked: pageStack.push(Qt.resolvedUrl("foil-ui/FoilUiGenerateKeyWarning.qml"), {
                foilUi: thisView.foilUi,
                allowedOrientations: page.allowedOrientations,
                acceptDestinationProperties: {
                    allowedOrientations: page.allowedOrientations,
                    mainPage: page,
                    foilUi: thisView.foilUi,
                    foilModel: foilModel
                },
                acceptDestinationAction: PageStackAction.Replace,
                acceptDestination: Qt.resolvedUrl("foil-ui/FoilUiGenerateKeyPage.qml")
            })
        }
    }

    Item {
        id: iconContainer

        width: iconLoader.width
        height: iconLoader.height
        x: (parent.width - width) / 2
        y: Math.max(Theme.paddingLarge, Math.min((thisView.height - height) / 2,
            thisView.height - height - loginLabel.height - 3 * Theme.paddingLarge -
            (_showPassword ? inputContainer.height : 0)))

        Loader {
            id: iconLoader

            sourceComponent: iconComponent
        }
        MouseArea {
            anchors.fill: parent
            enabled: deviceAuth.available && deviceAuth.enabled && !deviceAuth.busy
            onClicked: requestDeviceUnlock()
        }
    }

    Label {
        id: loginLabel

        x: Theme.horizontalPageMargin
        y: iconContainer.y + iconContainer.height + Theme.paddingLarge
        width: parent.width - 2 * x
        text: foilUi.qsTrEnterPasswordViewEnterPasswordShort()
        font.pixelSize: Theme.fontSizeLarge
        color: Theme.highlightColor
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    Column {
        id: inputContainer

        visible: _showPassword
        spacing: Theme.paddingSmall

        x: Theme.horizontalPageMargin
        y: loginLabel.y + loginLabel.height + Theme.paddingLarge
        width: parent.width - 2 * x

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            //: Password entry prompt after device authentication is canceled
            //% "Please enter your password"
            text: qsTrId("foilauth-biometric-enter-password")
            font.pixelSize: Theme.fontSizeMedium
            color: Theme.highlightColor
            wrapMode: Text.Wrap
        }

        HarbourPasswordInputField {
            id: inputField

            readonly property int _backgroundRuleTopOffset: editContentItem ? (editContentItem.y + editContentItem.height) : 0
            readonly property real _yAbs: inputContainer.y + y

            enabled: !_unlocking && !deviceAuth.busy
            onTextChanged: _wrongPassword = false
            EnterKey.onClicked: enterPassword()
            EnterKey.enabled: _canEnterPassword
        }

        Row {
            id: unlockButton

            spacing: Theme.paddingMedium
            anchors.horizontalCenter: parent.horizontalCenter
            Button {
                width: _canSave ? (inputContainer.width - Theme.paddingMedium) / 2 : Theme.buttonWidthMedium
                text: _unlocking ? foilUi.qsTrEnterPasswordViewButtonUnlocking() :
                    foilUi.qsTrEnterPasswordViewButtonUnlock()
                enabled: _canEnterPassword
                onClicked: enterPassword()
            }
            Button {
                width: (inputContainer.width - Theme.paddingMedium) / 2
                visible: _canSave
                //: Save the entered Foil password for device authentication
                //% "Save"
                text: qsTrId("foilauth-biometric-save")
                enabled: _canEnterPassword
                onClicked: savePassword()
            }
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: deviceAuth.available && deviceAuth.enabled
            //: Button label
            //% "Disable device unlock"
            text: qsTrId("foilauth-biometric-disable-device")
            enabled: !deviceAuth.busy && !_unlocking
            onClicked: {
                deviceAuth.disable()
                requestFocus()
            }
        }
    }

    function savePassword() {
        if (_canEnterPassword) {
            Qt.inputMethod.hide()
            deviceAuth.enroll(inputField.text)
            inputField.text = ""
        }
    }

    HarbourShakeAnimation  {
        id: wrongPasswordAnimation

        target: thisView
    }

    Loader {
        readonly property bool _display: _settings.sharedKeyWarning && foilUi.otherFoilAppsInstalled

        anchors {
            top: parent.top
            topMargin: _screenHeight - height - Theme.paddingLarge
            left: parent.left
            leftMargin: Theme.horizontalPageMargin
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
        }
        opacity: _display ? 1 : 0
        active: opacity > 0
        sourceComponent: Component {
            FoilUiAppsWarning {
                foilUi: thisView.foilUi
                onClicked: _settings.sharedKeyWarning = false
            }
        }
        Behavior on opacity { FadeAnimation {} }
    }

}
