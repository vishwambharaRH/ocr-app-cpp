import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuickTessApp 1.0
import App 1.0

ApplicationWindow {
    id: win
    visible: true
    width: 900
    height: 800
    title: "OCR + LLM Processor (Qt Quick)"

    OcrProcessor { id: processor }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Configuration group
        GroupBox { title: "Configuration"
            Layout.fillWidth: true
            ColumnLayout { spacing: 10; padding: 12
                RowLayout {
                    Label { text: "PDF:" }
                    TextField { id: pdfPath; Layout.fillWidth: true }
                    Button { text: "Browse"; onClicked: {
                        var fp = Qt.openUrlExternally ? "" : "" // placeholder
                        var file = Qt.platform.os === "windows" ? "" : ""
                        var res = Qt.createQmlObject("import QtQuick.Dialogs 1.3; FileDialog {}", win)
                        res.title = "Select PDF"
                        res.selectMultiple = false
                        res.nameFilters = ["PDF files (*.pdf)"]
                        res.onAccepted.connect(function(){ pdfPath.text = res.fileUrls[0].replace("file://",""); processor.selectPdf(pdfPath.text); })
                        res.open()
                    } }
                }

                RowLayout {
                    Label { text: "Output:" }
                    TextField { id: outPath; Layout.fillWidth: true }
                    Button { text: "Save As"; onClicked: {
                        var dlg = Qt.createQmlObject("import QtQuick.Dialogs 1.3; FileDialog {}", win)
                        dlg.title = "Save Output"
                        dlg.selectMultiple = false
                        dlg.acceptMode = 1
                        dlg.nameFilters = ["Text files (*.txt)"]
                        dlg.onAccepted.connect(function(){ outPath.text = dlg.fileUrl.replace("file://",""); processor.selectOutput(outPath.text); })
                        dlg.open()
                    } }
                }

                RowLayout {
                    Label { text: "OCR Engine:" }
                    ComboBox { id: engineBox; model: ["Tesseract","Google Vision"]; onCurrentTextChanged: processor.setOcrEngine(currentText) }
                    Label { text: "Tesseract Path:" }
                    TextField { id: tessPath; Layout.fillWidth: true; onTextChanged: processor.setTesseractPath(text) }
                    Button { text: "Browse"; onClicked: {
                        var fd = Qt.createQmlObject("import QtQuick.Dialogs 1.3; FileDialog {}", win)
                        fd.title = "Select Tesseract executable"
                        fd.folder = Qt.resolvedUrl(".")
                        fd.selectMultiple = false
                        fd.onAccepted.connect(function(){ tessPath.text = fd.fileUrl.replace("file://",""); processor.setTesseractPath(tessPath.text); })
                        fd.open()
                    } }
                }

                RowLayout {
                    Label { text: "Language:" }
                    ComboBox { id: langCombo; Layout.fillWidth: true; model: processor.languageOptions(); onCurrentTextChanged: processor.setLanguage(currentText) }
                }
            }
        }

        GroupBox { title: "LLM / Prompt"
            Layout.fillWidth: true
            ColumnLayout { spacing: 6; padding: 8
                RowLayout {
                    Label { text: "API Key:" }
                    TextField { id: keyField; Layout.fillWidth: true; echoMode: TextInput.Password; onTextChanged: processor.setApiKey(text) }
                }
                TextArea { id: promptArea; placeholderText: "Instruction for the LLM"; Layout.fillWidth: true; height: 120; onTextChanged: processor.setPrompt(text) }
            }
        }

        RowLayout {
            Button { text: "Start"; onClicked: processor.startProcessing() }
            Button { text: "Stop"; onClicked: processor.stopProcessing() }
            ProgressBar { id: prog; Layout.fillWidth: true; from: 0; to: 100 }
            Label { id: statusLabel; text: "" }
        }

        Rectangle { color: "transparent"; Layout.fillHeight: true; Layout.fillWidth: true }
    }

    Connections {
        target: processor
        onProgressChanged: { statusLabel.text = status; prog.value = percent }
        onFinished: { statusLabel.text = "Saved: " + outPath.text; }
        onErrorOccurred: { statusLabel.text = "ERROR: " + msg; }
    }
}
