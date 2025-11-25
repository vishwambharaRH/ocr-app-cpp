#include "OcrProcessor.h"
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QProcess>
#include <QDir>
#include <QDebug>
#include <QPdfDocument>
#include <QPdfPageRenderer>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QEventLoop>
#include <QCoreApplication>

OcrProcessor::OcrProcessor(QObject *parent)
    : QObject(parent),
      pdfDoc_(nullptr),
      startPage_(1),
      endPage_(-1),
      stopFlag_(false),
      netman_(new QNetworkAccessManager(this))
{
    // Initialize language map: visible -> (tess, vision)
    langMap_ = {
        { "English (eng)", { "eng", "en" } },
        { "Sanskrit – IAST / Devanagari (san)", { "san", "sa" } },
        { "Hindi (hin)", { "hin", "hi" } },
        { "Marathi (mar)", { "mar", "mr" } },
        { "Nepali (nep)", { "nep", "ne" } },
        { "Konkani (kok)", { "kok", "kok" } },
        { "Gujarati (guj)", { "guj", "gu" } },
        { "Punjabi – Gurmukhi (pan)", { "pan", "pa" } },
        { "Bengali (ben)", { "ben", "bn" } },
        { "Assamese (asm)", { "asm", "as" } },
        { "Odia (ori)", { "ori", "or" } },
        { "Telugu (tel)", { "tel", "te" } },
        { "Kannada (kan)", { "kan", "kn" } },
        { "Tamil (tam)", { "tam", "ta" } },
        { "Malayalam (mal)", { "mal", "ml" } },
        { "Sinhala (sin)", { "sin", "si" } }
    };
}

OcrProcessor::~OcrProcessor() {
    if (pdfDoc_) delete pdfDoc_;
}

QStringList OcrProcessor::languageOptions() const {
    return langMap_.keys();
}

void OcrProcessor::selectPdf(const QString &path) {
    pdfPath_ = path;
}

void OcrProcessor::selectOutput(const QString &path) {
    outputPath_ = path;
}

void OcrProcessor::setTesseractPath(const QString &path) {
    tessPath_ = path;
}

void OcrProcessor::setOcrEngine(const QString &engine) {
    ocrEngine_ = engine;
}

void OcrProcessor::setLanguage(const QString &langKey) {
    langKey_ = langKey;
}

void OcrProcessor::setApiKey(const QString &key) {
    apiKey_ = key;
}

void OcrProcessor::setPrompt(const QString &p) {
    prompt_ = p;
}

void OcrProcessor::setPageRange(int start, int end) {
    startPage_ = start;
    endPage_ = end;
}

void OcrProcessor::startProcessing() {
    if (pdfPath_.isEmpty()) { emit errorOccurred("No PDF selected."); return; }
    if (outputPath_.isEmpty()) { emit errorOccurred("No output location selected."); return; }
    if (ocrEngine_.isEmpty()) { emit errorOccurred("Choose OCR engine."); return; }
    if (langKey_.isEmpty()) langKey_ = "English (eng)";

    stopFlag_.store(false);

    // run in separate thread to keep UI responsive
    QMetaObject::invokeMethod(this, "workerRoutine", Qt::QueuedConnection);
}

void OcrProcessor::stopProcessing() {
    stopFlag_.store(true);
    emitProgress("Stopping...", 0);
}

QString OcrProcessor::renderPageToTempPNG(int pageIndex) {
    // Use QPdfDocument + QPdfPageRenderer to render a page to an image file
    if (!pdfDoc_) {
        pdfDoc_ = new QPdfDocument(this);
        if (pdfDoc_->load(pdfPath_) != QPdfDocument::Error::None)
        {
            throw std::runtime_error("Failed to open PDF");
        }
    }
    // pageIndex is 0-based
    QSizeF pageSize = pdfDoc_->pagePointSize(pageIndex);
    // Render at 300 DPI: point size -> pixels: points are 1/72 inch
    const double dpi = 300.0;
    double scale = dpi / 72.0;
    int w = int(pageSize.width() * scale);
    int h = int(pageSize.height() * scale);

    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(Qt::white);

#ifdef QPDFPAGE_RENDERER_AVAILABLE
    QPdfPageRenderer renderer;
    renderer.render(&image, pdfDoc_, pageIndex);
#else
    // Fallback: use QPdfDocument::render (Qt 6.5+)
    pdfDoc_->render(&image, pageIndex, image.size() );
#endif

    QTemporaryDir tmp;
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/qt_tess_tmp";
    QDir().mkpath(tempDir);

    // generate deterministic name
    QByteArray nameHash = QCryptographicHash::hash(QString("%1_%2").arg(pdfPath_).arg(pageIndex).toUtf8(), QCryptographicHash::Sha1);
    QString fname = QString("%1/page_%2_%3.png").arg(tempDir).arg(pageIndex).arg(QString(nameHash.toHex()).left(8));
    image.save(fname, "PNG");
    return fname;
}

QString OcrProcessor::runTesseractOnImage(const QString &imagePath, const QString &tessLang, const QString &tessdataDir) {
    // Use Tesseract C++ API. We will pass tessdataDir to Init so we guarantee which tessdata is used.
    tesseract::TessBaseAPI api;
    const char *datapath = tessdataDir.isEmpty() ? nullptr : tessdataDir.toUtf8().constData();
    if (api.Init(datapath, tessLang.toUtf8().constData())) {
        throw std::runtime_error(QString("Could not initialize tesseract for lang %1 (datapath=%2)").arg(tessLang, tessdataDir).toStdString());
    }

    Pix *image = pixRead(imagePath.toUtf8().constData());
    if (!image) {
        api.End();
        throw std::runtime_error("Failed to read image into Leptonica Pix object.");
    }

    api.SetImage(image);
    api.Recognize(0);
    char *out = api.GetUTF8Text();
    QString result;
    if (out) {
        result = QString::fromUtf8(out);
        delete [] out;
    }
    pixDestroy(&image);
    api.End();
    return result;
}

QString OcrProcessor::runGoogleVisionOnImage(const QString &imagePath, const QString &visionLang) {
    // Using REST API: https://vision.googleapis.com/v1/images:annotate?key=API_KEY
    // API key expected in apiKey_. For production service-account flow, you should exchange a service account for OAuth token.
    if (apiKey_.isEmpty()) {
        throw std::runtime_error("Google Vision requires an API key (set in API Key field).");
    }
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Failed to open rendered image for Vision.");
    QByteArray bytes = f.readAll();
    f.close();
    QString base64 = QString::fromLatin1(bytes.toBase64());

    QJsonObject imageObj;
    imageObj["content"] = base64;

    QJsonObject feature;
    feature["type"] = "DOCUMENT_TEXT_DETECTION";

    QJsonArray features;
    features.append(feature);

    QJsonObject request;
    request["image"] = imageObj;
    request["features"] = features;

    // Add languageHints in imageContext
    QJsonObject imageContext;
    QJsonArray langHints;
    langHints.append(visionLang);
    imageContext["languageHints"] = langHints;
    request["imageContext"] = imageContext;

    QJsonArray requests;
    requests.append(request);
    QJsonObject payload;
    payload["requests"] = requests;

    QNetworkRequest netReq(QUrl(QString("https://vision.googleapis.com/v1/images:annotate?key=%1").arg(apiKey_)));
    netReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = netman_->post(netReq, QJsonDocument(payload).toJson());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        QString err = reply->errorString();
        reply->deleteLater();
        throw std::runtime_error(err.toStdString());
    }
    QByteArray resp = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(resp);
    if (!doc.isObject()) throw std::runtime_error("Invalid response from Google Vision.");
    QJsonObject root = doc.object();
    QJsonArray responses = root["responses"].toArray();
    if (responses.isEmpty()) return QString();

    QJsonObject first = responses[0].toObject();
    QString fullText = first["fullTextAnnotation"].toObject()["text"].toString();
    return fullText;
}

QStringList OcrProcessor::splitTextIntoBatches(const QString &text, int wordsPerBatch) {
    QStringList words = text.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
    QStringList batches;
    for (int i = 0; i < words.size(); i += wordsPerBatch) {
        int count = qMin(wordsPerBatch, words.size() - i);
        batches.append(words.mid(i, count).join(" "));
    }
    return batches;
}

QString OcrProcessor::callLLM(const QString &textChunk, const QString &batchInfo) {
    // Use OpenAI REST ChatCompletions or OpenRouter (simple logic)
    // apiKey_ expected
    if (apiKey_.isEmpty()) throw std::runtime_error("LLM API key required.");

    // Determine provider: if apiKey_ contains "openrouter:" prefix user could have chosen; for now assume OpenAI unless user uses openrouter URL
    // We'll inspect prompt_: if llm provider mention is set inside prompt (or better add a UI field). For brevity, assume OpenAI.
    QJsonObject messageUser;
    messageUser["role"] = "user";
    messageUser["content"] = QString("%1\n\nPlease process the following text content %2:\n\n---\n%3\n---")
                                .arg(prompt_, batchInfo, textChunk);

    QJsonArray messages;
    messages.append(messageUser);
    QJsonObject system;
    system["role"] = "system";
    system["content"] = "You are an expert assistant.";

    QJsonArray messagesArray;
    messagesArray.append(system);
    messagesArray.append(messageUser);

    QJsonObject payload;
    payload["model"] = QString("gpt-4o"); // change as needed
    payload["messages"] = messagesArray;

    QNetworkRequest req(QUrl("https://api.openai.com/v1/chat/completions"));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey_).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = netman_->post(req, QJsonDocument(payload).toJson());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        QString err = reply->errorString();
        reply->deleteLater();
        throw std::runtime_error(err.toStdString());
    }
    QByteArray resp = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(resp);
    if (!doc.isObject()) throw std::runtime_error("Invalid response from OpenAI.");
    QJsonObject root = doc.object();
    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) return QString();
    QString text = choices[0].toObject()["message"].toObject()["content"].toString();
    return text;
}

void OcrProcessor::workerRoutine() {
    try {
        emitProgress("Loading PDF...", 2);
        QPdfDocument doc;
        if (doc.load(pdfPath_) != QPdfDocument::Error::None) throw std::runtime_error("Failed to open PDF");
        int totalPages = doc.pageCount();
        int s = (startPage_ >= 1) ? startPage_ : 1;
        int e = (endPage_ >= 1) ? endPage_ : totalPages;
        if (s < 1) s = 1;
        if (e > totalPages) e = totalPages;
        if (s > e) throw std::runtime_error("Invalid page range");

        QList<QString> images;
        int pageCount = e - s + 1;
        for (int i = s - 1; i <= e - 1; ++i) {
            if (stopFlag_.load()) throw std::runtime_error("Process stopped by user.");
            emitProgress(QString("Rendering page %1/%2...").arg(i - (s - 1) + 1).arg(pageCount), 5 + (i - (s - 1)) * 5.0);
            QString png = renderPageToTempPNG(i);
            images.append(png);
        }

        if (stopFlag_.load()) throw std::runtime_error("Process stopped by user.");

        emitProgress("Performing OCR...", 20);
        QStringList ocr_results;
        auto langPair = langMap_.value(langKey_, qMakePair(QString("eng"), QString("en")));
        QString tessLang = langPair.first;
        QString visionLang = langPair.second;

        // Determine tessdata dir: If APP_TESSDATA_DIR defined in compile or tessPath_ used to compute parent
        QString tessdataDir;
#ifdef APP_TESSDATA_DIR
        tessdataDir = QString(APP_TESSDATA_DIR);
#else
        // If tessPath_ is set and ends with 'tesseract' or 'tesseract.exe' try to infer tessdata dir
        if (!tessPath_.isEmpty()) {
            QFileInfo fi(tessPath_);
            QString binDir = fi.absolutePath();
            // common Homebrew layout: ../opt/tesseract/bin/tesseract -> ../opt/tesseract/share/tessdata
            QDir d(binDir);
            d.cdUp();
            if (d.exists("share/tessdata")) tessdataDir = d.absoluteFilePath("share/tessdata");
        }
#endif

        for (int i = 0; i < images.size(); ++i) {
            if (stopFlag_.load()) throw std::runtime_error("Process stopped by user.");
            emitProgress(QString("OCR page %1/%2...").arg(i+1).arg(images.size()), 20 + (i+1)*(30.0/images.size()));
            QString imagePath = images[i];
            QString text;
            if (ocrEngine_ == "Tesseract") {
                text = runTesseractOnImage(imagePath, tessLang, tessdataDir);
            } else {
                text = runGoogleVisionOnImage(imagePath, visionLang);
            }
            ocr_results << text;
        }

        if (stopFlag_.load()) throw std::runtime_error("Process stopped by user.");

        QString fullText = ocr_results.join("\n\n");

        if (prompt_.isEmpty()) {
            // Just save OCR result
            QFile outf(outputPath_);
            if (!outf.open(QIODevice::WriteOnly | QIODevice::Text)) throw std::runtime_error("Failed to open output file for writing.");
            outf.write(fullText.toUtf8());
            outf.close();
            emitProgress("Done", 100);
            emit finished(outputPath_);
            return;
        }

        // LLM processing
        emitProgress("Splitting text into batches...", 40);
        QStringList batches = splitTextIntoBatches(fullText);
        QStringList llmOut;
        for (int i = 0; i < batches.size(); ++i) {
            if (stopFlag_.load()) throw std::runtime_error("Process stopped by user.");
            double prog = 40 + ((i+1) / double(batches.size())) * 55;
            emitProgress(QString("Calling LLM (batch %1/%2)").arg(i+1).arg(batches.size()), prog);
            QString batchInfo = QString("(Batch %1 of %2)").arg(i+1).arg(batches.size());
            QString res = callLLM(batches[i], batchInfo);
            llmOut << res;
        }

        QString finalOutput = llmOut.join("\n\n---\n\n");
        QFile outf(outputPath_);
        if (!outf.open(QIODevice::WriteOnly | QIODevice::Text)) throw std::runtime_error("Failed to open output file for writing.");
        outf.write(finalOutput.toUtf8());
        outf.close();

        emitProgress("Done", 100);
        emit finished(outputPath_);
    } catch (const std::exception &ex) {
        emit errorOccurred(ex.what());
    } catch (...) {
        emit errorOccurred("Unknown error during processing.");
    }
}
