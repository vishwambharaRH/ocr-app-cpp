import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import App 1.0

ApplicationWindow {
    id: win
    visible: true
    width: 900
    height: 850
    minimumWidth: 800
    minimumHeight: 650
    title: "OCR + LLM Document Processor"

    OcrProcessor { id: processor }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 25
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 10

            // Configuration GroupBox
            GroupBox {
                title: "Configuration"
                Layout.fillWidth: true
                padding: 20

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    // PDF Document
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "PDF Document:"; Layout.preferredWidth: 120 }
                        TextField {
                            id: pdfPath
                            Layout.fillWidth: true
                            readOnly: true
                            placeholderText: "Select a PDF file..."
                        }
                        Button {
                            text: "Browse..."
                            onClicked: pdfDialog.open()
                        }
                    }

                    // Output File
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Output File:"; Layout.preferredWidth: 120 }
                        TextField {
                            id: outPath
                            Layout.fillWidth: true
                            readOnly: true
                            placeholderText: "Choose output location..."
                        }
                        Button {
                            text: "Save As..."
                            onClicked: outputDialog.open()
                        }
                    }

                    // Page Range
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Page Range:"; Layout.preferredWidth: 120 }
                        Label { text: "Start:" }
                        TextField {
                            id: startPage
                            placeholderText: "1"
                            Layout.preferredWidth: 60
                            validator: IntValidator { bottom: 1 }
                            onTextChanged: {
                                var val = parseInt(text);
                                if (!isNaN(val) && val >= 1) {
                                    processor.setPageRange(val, parseInt(endPage.text) || -1);
                                }
                            }
                        }
                        Label { text: "End:" }
                        TextField {
                            id: endPage
                            placeholderText: "Last"
                            Layout.preferredWidth: 60
                            validator: IntValidator { bottom: 1 }
                            onTextChanged: {
                                var val = parseInt(text);
                                processor.setPageRange(parseInt(startPage.text) || 1, isNaN(val) ? -1 : val);
                            }
                        }
                        Label {
                            text: "(leave blank for all pages)"
                            font.italic: true
                            Layout.fillWidth: true
                        }
                    }

                    // OCR Engine
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "OCR Engine:"; Layout.preferredWidth: 120 }
                        ComboBox {
                            id: engineBox
                            model: ["Tesseract", "Google Vision"]
                            Layout.preferredWidth: 200
                            onCurrentTextChanged: {
                                processor.setOcrEngine(currentText);
                                tessPathRow.visible = (currentText === "Tesseract");
                                googleKeyRow.visible = (currentText === "Google Vision");
                            }
                        }
                    }

                    // Tesseract Path
                    RowLayout {
                        id: tessPathRow
                        Layout.fillWidth: true
                        visible: engineBox.currentText === "Tesseract"
                        Label { text: "Tesseract Path:"; Layout.preferredWidth: 120 }
                        TextField {
                            id: tessPath
                            Layout.fillWidth: true
                            placeholderText: "Path to tesseract executable..."
                            onTextChanged: processor.setTesseractPath(text)
                        }
                        Button {
                            text: "Browse..."
                            onClicked: tessDialog.open()
                        }
                    }

                    // Google Vision Service Account JSON
                    RowLayout {
                        id: googleKeyRow
                        Layout.fillWidth: true
                        visible: engineBox.currentText === "Google Vision"
                        Label { text: "Google Vision Key (JSON):"; Layout.preferredWidth: 120 }
                        TextField {
                            id: googleKey
                            Layout.fillWidth: true
                            readOnly: true
                            placeholderText: "Select service-account JSON file..."
                        }
                        Button {
                            text: "Browse..."
                            onClicked: googleJsonDialog.open()
                        }
                    }

                    // OCR Language
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "OCR Language:"; Layout.preferredWidth: 120 }
                        ComboBox {
                            id: langCombo
                            Layout.fillWidth: true
                            model: processor.languageOptions()
                            currentIndex: 0
                            onCurrentTextChanged: processor.setLanguage(currentText)
                        }
                    }
                }
            }

            // OCR Only Checkbox
            CheckBox {
                id: ocrOnlyCheck
                text: "Perform OCR Only (No LLM)"
                onCheckedChanged: {
                    processor.setOcrOnly(checked);
                    llmFrame.visible = !checked;
                }
            }

            // LLM Processing GroupBox
            GroupBox {
                id: llmFrame
                title: "LLM Processing"
                Layout.fillWidth: true
                padding: 20

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    // LLM Provider
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "LLM Provider:"; Layout.preferredWidth: 120 }
                        ComboBox {
                            id: llmDropdown
                            Layout.fillWidth: true
                            model: ["OpenAI: gpt-4o", "OpenRouter: deepseek/deepseek-chat"]
                            onCurrentTextChanged: {
                                processor.setLlmProvider(currentText);
                                if (currentText.includes("OpenAI")) {
                                    apiKeyLabel.text = "OpenAI API Key:";
                                } else if (currentText.includes("OpenRouter")) {
                                    apiKeyLabel.text = "OpenRouter API Key:";
                                } else {
                                    apiKeyLabel.text = "LLM API Key:";
                                }
                            }
                        }
                    }

                    // API Key
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            id: apiKeyLabel
                            text: "OpenAI API Key:"
                            Layout.preferredWidth: 120
                        }
                        TextField {
                            id: keyField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            placeholderText: "Enter your API key..."
                            onTextChanged: processor.setApiKey(text)
                        }
                    }

                    // LLM Prompt
                    GroupBox {
                        title: "LLM Prompt"
                        Layout.fillWidth: true
                        padding: 15

                        ColumnLayout {
                            width: parent.width

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 150

                                TextArea {
                                    id: promptArea
                                    placeholderText: "Enter instructions for the LLM to process the OCR text...\n\nExample: Extract all names and dates mentioned in the document."
                                    wrapMode: TextArea.Wrap
                                    font.pixelSize: 11
                                    onTextChanged: processor.setPrompt(text)
                                }
                            }
                        }
                    }
                }
            }

            // Action Buttons and Progress
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 10
                spacing: 10

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10

                    Button {
                        id: startButton
                        text: "Start Processing"
                        highlighted: true
                        font.bold: true
                        onClicked: {
                            processor.startProcessing();
                            startButton.enabled = false;
                            stopButton.enabled = true;
                        }
                    }

                    Button {
                        id: stopButton
                        text: "Stop"
                        enabled: false
                        onClicked: {
                            processor.stopProcessing();
                            stopButton.enabled = false;
                            // Immediately re-enable Start so user can retry while worker stops
                            startButton.enabled = true;
                        }
                    }
                }

                Label {
                    id: statusLabel
                    text: "Ready"
                    font.italic: true
                    Layout.alignment: Qt.AlignHCenter
                }

                ProgressBar {
                    id: prog
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: 0
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    // File Dialogs
    FileDialog {
        id: pdfDialog
        title: "Select PDF Document"
        nameFilters: ["PDF files (*.pdf)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            var path = selectedFile.toString().replace("file://", "");
            pdfPath.text = path;
            processor.selectPdf(path);
        }
    }

    FileDialog {
        id: outputDialog
        title: "Save Output File"
        nameFilters: ["Text files (*.txt)", "All files (*)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        onAccepted: {
            var path = selectedFile.toString().replace("file://", "");
            outPath.text = path;
            processor.selectOutput(path);
        }
    }

    FileDialog {
        id: tessDialog
        title: "Select Tesseract Executable"
        nameFilters: ["Executables (tesseract tesseract.exe)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            var path = selectedFile.toString().replace("file://", "");
            tessPath.text = path;
            processor.setTesseractPath(path);
        }
    }

    FileDialog {
        id: googleJsonDialog
        title: "Select Google Service Account JSON"
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            var path = selectedFile.toString().replace("file://", "");
            googleKey.text = path;
            processor.setGoogleServiceAccountPath(path);
        }
    }

    // Connections to processor signals
    Connections {
        target: processor

        function onProgressChanged(status, percent) {
            statusLabel.text = status;
            prog.value = percent;
        }

        function onFinished(outPath) {
            statusLabel.text = "Processing complete! Output saved to: " + outPath;
            prog.value = 100;
            startButton.enabled = true;
            stopButton.enabled = false;
            
            completionDialog.visible = true;
        }

        function onErrorOccurred(msg) {
            statusLabel.text = "Error: " + msg;
            prog.value = 0;
            startButton.enabled = true;
            stopButton.enabled = false;
            
            errorDialog.text = msg;
            errorDialog.visible = true;
        }

        function onStopped() {
            statusLabel.text = "Stopped";
            prog.value = 0;
            startButton.enabled = true;
            stopButton.enabled = false;
        }
    }

    // Completion Dialog
    Dialog {
        id: completionDialog
        title: "Success"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok

        Label {
            text: "Processing complete!\nOutput saved to:\n" + outPath.text
            wrapMode: Text.WordWrap
        }
    }

    // Error Dialog
    Dialog {
        id: errorDialog
        title: "Error"
        property alias text: errorLabel.text
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok

        Label {
            id: errorLabel
            wrapMode: Text.WordWrap
            width: 400
        }
    }

    Component.onCompleted: {
        // Auto-detect Tesseract on startup
        var tessPathCandidates = [];
        
        if (Qt.platform.os === "windows") {
            tessPathCandidates = ["C:/Program Files/Tesseract-OCR/tesseract.exe"];
        } else if (Qt.platform.os === "osx") {
            tessPathCandidates = ["/opt/homebrew/bin/tesseract", "/usr/local/bin/tesseract"];
        } else {
            tessPathCandidates = ["/usr/bin/tesseract", "/usr/local/bin/tesseract"];
        }
        
        // This would require a helper function in C++ to check file existence
        // For now, just set a default path hint
        if (Qt.platform.os === "osx") {
            tessPath.placeholderText = "/opt/homebrew/bin/tesseract";
        } else if (Qt.platform.os === "windows") {
            tessPath.placeholderText = "C:/Program Files/Tesseract-OCR/tesseract.exe";
        }
    }
}