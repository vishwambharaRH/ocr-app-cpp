#include "OcrProcessor.h"
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QEventLoop>
#include <QFileInfo>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <stdexcept>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <ctime>

// -----------------------------------------------------------------------------
// Worker object: performs heavy OCR/LLM work on a background thread.
class OcrWorker : public QObject {
    Q_OBJECT
public:
        OcrWorker(const QString &pdfPath,
                            const QString &outputPath,
                            const QString &tessPath,
                            const QString &ocrEngine,
                            const QString &langKey,
                            const QString &apiKey,
                            const QString &oauthToken,
                            const QString &googleServiceAccountPath,
                            const QString &prompt,
                            const QMap<QString, QPair<QString, QString>> &langMap,
                            int startPage,
                            int endPage,
                            std::atomic<bool> *stopFlag)
                    : pdfPath_(pdfPath), outputPath_(outputPath), tessPath_(tessPath),
                        ocrEngine_(ocrEngine), langKey_(langKey), apiKey_(apiKey),
                        oauthToken_(oauthToken), googleServiceAccountPath_(googleServiceAccountPath), 
                        prompt_(prompt), langMap_(langMap), startPage_(startPage), endPage_(endPage), stopFlag_(stopFlag) {}

signals:
    void progressChanged(QString, double);
    void finished(QString);
    void errorOccurred(QString);

public slots:
    void process() {
        try {
            emit progressChanged("Loading PDF...", 2);
            QPdfDocument doc;
            if (doc.load(pdfPath_) != QPdfDocument::Error::None) {
                throw std::runtime_error("Failed to open PDF");
            }

            int totalPages = doc.pageCount();
            int s = (startPage_ >= 1) ? startPage_ : 1;
            int e = (endPage_ >= 1) ? endPage_ : totalPages;

            if (s < 1) s = 1;
            if (e > totalPages) e = totalPages;
            if (s > e) {
                throw std::runtime_error("Invalid page range");
            }

            QStringList images;
            for (int i = s - 1; i < e; ++i) {
                if (stopFlag_ && stopFlag_->load()) throw std::runtime_error("Process stopped by user.");
                emit progressChanged(QString("Rendering page %1/%2...").arg(i - (s - 1) + 1).arg(e - (s - 1)), 5);
                
                QSizeF pageSize = doc.pagePointSize(i);
                const double dpi = 300.0;
                double scale = dpi / 72.0;
                int w = static_cast<int>(pageSize.width() * scale);
                int h = static_cast<int>(pageSize.height() * scale);
                QImage image = doc.render(i, QSize(w, h));
                if (image.isNull()) throw std::runtime_error("Failed to render PDF page");
                
                QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/qt_tess_tmp";
                QDir().mkpath(tempDir);
                QString fname = QString("%1/page_%2.png").arg(tempDir).arg(i);
                if (!image.save(fname, "PNG")) throw std::runtime_error("Failed to save rendered page");
                images.append(fname);
            }

            // Perform OCR
            emit progressChanged("Performing OCR...", 20);
            QStringList ocrResults;
            auto langPair = langMap_.value(langKey_, qMakePair(QString("eng"), QString("en")));
            QString tessLang = langPair.first;

            for (int i = 0; i < images.size(); ++i) {
                if (stopFlag_ && stopFlag_->load()) {
                    // Clean up temp files before stopping
                    for (const QString &img : images) {
                        QFile::remove(img);
                    }
                    throw std::runtime_error("Process stopped by user.");
                }
                
                emit progressChanged(QString("OCR page %1/%2...").arg(i + 1).arg(images.size()), 
                                   20 + ((i + 1.0) / images.size()) * 30);
                QString text;
                
                if (ocrEngine_ == "Tesseract") {
                    tesseract::TessBaseAPI api;
                    const char *datapath = tessPath_.isEmpty() ? nullptr : tessPath_.toUtf8().constData();
                    if (api.Init(datapath, tessLang.toUtf8().constData())) {
                        // Clean up before throwing
                        for (const QString &img : images) {
                            QFile::remove(img);
                        }
                        throw std::runtime_error("Could not initialize tesseract");
                    }
                    Pix *image = pixRead(images[i].toUtf8().constData());
                    if (!image) { 
                        api.End(); 
                        for (const QString &img : images) {
                            QFile::remove(img);
                        }
                        throw std::runtime_error("Failed to read image"); 
                    }
                    api.SetImage(image); 
                    api.Recognize(0);
                    char *out = api.GetUTF8Text();
                    if (out) { 
                        text = QString::fromUtf8(out); 
                        delete[] out; 
                    }
                    pixDestroy(&image); 
                    api.End();
                } else if (ocrEngine_ == "Google Vision") {
                    // Google Vision implementation
                    QFile f(images[i]); 
                    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Failed to open image");
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
                    QJsonArray requests; 
                    requests.append(request);
                    QJsonObject payload; 
                    payload["requests"] = requests;

                    QNetworkAccessManager netman;
                    QNetworkRequest req;
                    if (!oauthToken_.isEmpty()) {
                        req.setUrl(QUrl("https://vision.googleapis.com/v1/images:annotate"));
                        req.setRawHeader("Authorization", QString("Bearer %1").arg(oauthToken_).toUtf8());
                    } else {
                        throw std::runtime_error("Service account usage not available in this worker path");
                    }
                    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                    QNetworkReply *reply = netman.post(req, QJsonDocument(payload).toJson());
                    QEventLoop loop; 
                    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit); 
                    loop.exec();
                    
                    if (reply->error() != QNetworkReply::NoError) { 
                        QString err = reply->errorString(); 
                        reply->deleteLater(); 
                        for (const QString &img : images) {
                            QFile::remove(img);
                        }
                        throw std::runtime_error(err.toStdString()); 
                    }
                    
                    QByteArray resp = reply->readAll(); 
                    reply->deleteLater(); 
                    QJsonDocument doc = QJsonDocument::fromJson(resp);
                    if (!doc.isObject()) throw std::runtime_error("Invalid response from Google Vision.");
                    
                    QJsonObject root = doc.object(); 
                    QJsonArray responses = root["responses"].toArray(); 
                    if (responses.isEmpty()) { 
                        text = QString(); 
                    } else { 
                        text = responses[0].toObject()["fullTextAnnotation"].toObject()["text"].toString(); 
                    }
                }
                
                ocrResults << text;
                QFile::remove(images[i]);
            }

            QString joined = ocrResults.join("\n\n");
            QFile outf(outputPath_);
            if (!outf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                throw std::runtime_error("Failed to open output file for writing.");
            }
            outf.write(joined.toUtf8()); 
            outf.close();

            emit progressChanged("Done", 100);
            emit finished(outputPath_);
            
        } catch (const std::exception &ex) {
            emit errorOccurred(QString::fromStdString(ex.what()));
        } catch (...) {
            emit errorOccurred("Unknown error during processing.");
        }
    }

private:
    QString pdfPath_;
    QString outputPath_;
    QString tessPath_;
    QString ocrEngine_;
    QString langKey_;
    QString apiKey_;
    QString oauthToken_;
    QString googleServiceAccountPath_;
    QString prompt_;
    QMap<QString, QPair<QString, QString>> langMap_;
    int startPage_;
    int endPage_;
    std::atomic<bool> *stopFlag_;
};

// Helper functions
static QByteArray base64UrlEncode(const QByteArray &input) {
    QByteArray b = input.toBase64(QByteArray::Base64Encoding);
    b = b.replace('+', '-').replace('/', '_');
    while (b.endsWith('=')) b.chop(1);
    return b;
}

#include "OcrProcessor.moc"

static QByteArray signWithPrivateKey(const QByteArray &privateKeyPem, const QByteArray &data) {
    BIO *bio = BIO_new_mem_buf(privateKeyPem.constData(), privateKeyPem.size());
    if (!bio) return QByteArray();
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) return QByteArray();

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        EVP_PKEY_free(pkey);
        return QByteArray();
    }

    if (EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, pkey) != 1) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return QByteArray();
    }

    if (EVP_DigestSignUpdate(mdctx, data.constData(), data.size()) != 1) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return QByteArray();
    }

    size_t siglen = 0;
    if (EVP_DigestSignFinal(mdctx, NULL, &siglen) != 1) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return QByteArray();
    }

    unsigned char *sig = (unsigned char *)OPENSSL_malloc(siglen);
    if (!sig) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return QByteArray();
    }

    if (EVP_DigestSignFinal(mdctx, sig, &siglen) != 1) {
        OPENSSL_free(sig);
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return QByteArray();
    }

    QByteArray signature = QByteArray(reinterpret_cast<char*>(sig), (int)siglen);
    OPENSSL_free(sig);
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return signature;
}

OcrProcessor::OcrProcessor(QObject *parent)
    : QObject(parent),
      pdfDoc_(nullptr),
      startPage_(1),
      endPage_(-1),
      ocrOnly_(false),
      stopFlag_(false),
      workerThread_(nullptr),
      netman_(new QNetworkAccessManager(this))
{
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
    
    llmProvider_ = "OpenAI: gpt-4o";
}

OcrProcessor::~OcrProcessor() {
    if (pdfDoc_) {
        delete pdfDoc_;
    }
    if (workerThread_) {
        stopFlag_.store(true);
        workerThread_->quit();
        workerThread_->wait(3000); // Wait up to 3 seconds
        if (workerThread_->isRunning()) {
            workerThread_->terminate();
            workerThread_->wait();
        }
        delete workerThread_;
    }
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

void OcrProcessor::setGoogleServiceAccountPath(const QString &path) {
    googleServiceAccountPath_ = path;
}

QString OcrProcessor::getAccessTokenFromServiceAccount(const QString &jsonPath) {
    qint64 now = std::time(nullptr);
    if (!googleAccessToken_.isEmpty() && googleAccessTokenExpiry_ > now + 60) {
        return googleAccessToken_;
    }

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Failed to open service account JSON file.");
    }
    QByteArray content = f.readAll();
    f.close();

    QJsonDocument jd = QJsonDocument::fromJson(content);
    if (!jd.isObject()) {
        throw std::runtime_error("Invalid service account JSON.");
    }
    QJsonObject obj = jd.object();
    QString client_email = obj.value("client_email").toString();
    QString private_key = obj.value("private_key").toString();
    if (client_email.isEmpty() || private_key.isEmpty()) {
        throw std::runtime_error("Service account JSON missing required fields.");
    }

    qint64 iat = std::time(nullptr);
    qint64 exp = iat + 3600;

    QJsonObject header;
    header["alg"] = "RS256";
    header["typ"] = "JWT";

    QJsonObject claim;
    claim["iss"] = client_email;
    claim["scope"] = "https://www.googleapis.com/auth/cloud-platform";
    claim["aud"] = "https://oauth2.googleapis.com/token";
    claim["exp"] = (double)exp;
    claim["iat"] = (double)iat;

    QByteArray headerB = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray claimB = QJsonDocument(claim).toJson(QJsonDocument::Compact);

    QByteArray encodedHeader = base64UrlEncode(headerB);
    QByteArray encodedClaim = base64UrlEncode(claimB);
    QByteArray unsignedJwt = encodedHeader + "." + encodedClaim;

    QByteArray signature = signWithPrivateKey(private_key.toUtf8(), unsignedJwt);
    if (signature.isEmpty()) {
        throw std::runtime_error("Failed to sign JWT assertion.");
    }
    QByteArray encodedSig = base64UrlEncode(signature);
    QByteArray signedJwt = unsignedJwt + "." + encodedSig;

    QNetworkRequest req(QUrl("https://oauth2.googleapis.com/token"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QByteArray body = QString("grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=%1")
        .arg(QString::fromUtf8(signedJwt)).toUtf8();

    QNetworkReply *reply = netman_->post(req, body);
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
    QJsonDocument respDoc = QJsonDocument::fromJson(resp);
    if (!respDoc.isObject()) {
        throw std::runtime_error("Invalid token response from OAuth server.");
    }
    QJsonObject robj = respDoc.object();
    QString access_token = robj.value("access_token").toString();
    int expires_in = robj.value("expires_in").toInt(3600);
    if (access_token.isEmpty()) {
        throw std::runtime_error("OAuth token response missing access_token.");
    }

    googleAccessToken_ = access_token;
    googleAccessTokenExpiry_ = std::time(nullptr) + expires_in;
    return googleAccessToken_;
}

void OcrProcessor::setPrompt(const QString &p) {
    prompt_ = p;
}

void OcrProcessor::setPageRange(int start, int end) {
    startPage_ = start;
    endPage_ = end;
}

void OcrProcessor::setOcrOnly(bool ocrOnly) {
    ocrOnly_ = ocrOnly;
}

void OcrProcessor::setLlmProvider(const QString &provider) {
    llmProvider_ = provider;
}

void OcrProcessor::startProcessing() {
    // Validation with proper error messages
    if (pdfPath_.isEmpty()) {
        emit errorOccurred("No PDF file selected. Please choose a PDF document.");
        return;
    }
    
    QFileInfo pdfInfo(pdfPath_);
    if (!pdfInfo.exists() || !pdfInfo.isFile()) {
        emit errorOccurred(QString("PDF file not found: %1").arg(pdfPath_));
        return;
    }
    
    if (outputPath_.isEmpty()) {
        emit errorOccurred("No output location selected. Please choose where to save the results.");
        return;
    }
    
    if (ocrEngine_.isEmpty()) {
        emit errorOccurred("No OCR engine selected. Please choose Tesseract or Google Vision.");
        return;
    }
    
    // Tesseract-specific validation
    if (ocrEngine_ == "Tesseract") {
        if (tessPath_.isEmpty()) {
            emit errorOccurred("Tesseract path not set. Please specify the location of the Tesseract executable.");
            return;
        }
        
        QFileInfo tessInfo(tessPath_);
        if (!tessInfo.exists()) {
            emit errorOccurred(QString("Tesseract executable not found at: %1\n\nPlease verify the path is correct.").arg(tessPath_));
            return;
        }
        
        if (!tessInfo.isFile()) {
            emit errorOccurred(QString("The specified Tesseract path is not a file: %1").arg(tessPath_));
            return;
        }
        
        if (!(tessInfo.permissions() & QFileDevice::ExeUser)) {
            emit errorOccurred(QString("Tesseract executable lacks execute permissions: %1").arg(tessPath_));
            return;
        }
    }
    
    // Google Vision validation
    if (ocrEngine_ == "Google Vision") {
        if (apiKey_.isEmpty() && googleServiceAccountPath_.isEmpty()) {
            emit errorOccurred("Google Vision requires either an API key or a service account JSON file.");
            return;
        }
        
        if (!googleServiceAccountPath_.isEmpty()) {
            QFileInfo jsonInfo(googleServiceAccountPath_);
            if (!jsonInfo.exists() || !jsonInfo.isFile()) {
                emit errorOccurred(QString("Service account JSON file not found: %1").arg(googleServiceAccountPath_));
                return;
            }
        }
    }
    
    // LLM validation
    if (!ocrOnly_) {
        if (apiKey_.isEmpty()) {
            emit errorOccurred("API key required for LLM processing. Either enter an API key or enable 'OCR Only' mode.");
            return;
        }
        if (prompt_.isEmpty()) {
            emit errorOccurred("LLM prompt required. Please enter instructions for processing the OCR text.");
            return;
        }
    }
    
    if (langKey_.isEmpty()) {
        langKey_ = "English (eng)";
    }

    // Stop any existing worker
    if (workerThread_ && workerThread_->isRunning()) {
        stopFlag_.store(true);
        workerThread_->quit();
        workerThread_->wait(1000);
        if (workerThread_->isRunning()) {
            workerThread_->terminate();
            workerThread_->wait();
        }
    }

    stopFlag_.store(false);

    if (workerThread_) {
        delete workerThread_;
        workerThread_ = nullptr;
    }

    workerThread_ = new QThread();
    
    QString oauthToken;
    if (ocrEngine_ == "Google Vision" && apiKey_.isEmpty() && !googleServiceAccountPath_.isEmpty()) {
        try {
            emit progressChanged("Obtaining Google Cloud access token...", 1);
            oauthToken = getAccessTokenFromServiceAccount(googleServiceAccountPath_);
        } catch (const std::exception &ex) {
            emit errorOccurred(QString("Failed to authenticate with Google Cloud: %1").arg(ex.what()));
            delete workerThread_;
            workerThread_ = nullptr;
            return;
        } catch (...) {
            emit errorOccurred("Failed to obtain access token from service account.");
            delete workerThread_;
            workerThread_ = nullptr;
            return;
        }
    }

    OcrWorker *worker = new OcrWorker(pdfPath_, outputPath_, tessPath_, ocrEngine_, langKey_, 
                                      apiKey_, oauthToken, googleServiceAccountPath_, prompt_, 
                                      langMap_, startPage_, endPage_, &stopFlag_);
    worker->moveToThread(workerThread_);

    connect(worker, &OcrWorker::progressChanged, this, &OcrProcessor::progressChanged, Qt::QueuedConnection);
    connect(worker, &OcrWorker::finished, this, [this, worker](QString out) {
        emit this->finished(out);
        this->workerThread_->quit();
        worker->deleteLater();
    }, Qt::QueuedConnection);
    connect(worker, &OcrWorker::errorOccurred, this, [this, worker](QString err) {
        emit this->errorOccurred(err);
        this->workerThread_->quit();
        worker->deleteLater();
    }, Qt::QueuedConnection);

    connect(workerThread_, &QThread::started, worker, &OcrWorker::process);
    connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);
    connect(workerThread_, &QThread::finished, this, [this]() {
        emit stopped();
        this->workerThread_ = nullptr;
    });
    
    workerThread_->start();
}

void OcrProcessor::stopProcessing() {
    if (workerThread_ && workerThread_->isRunning()) {
        stopFlag_.store(true);
        emitProgress("Stopping...", 0);
    } else {
        emit stopped();
    }
}

#include "utils.h"

QString OcrProcessor::getTessdataDir() {
#ifdef APP_TESSDATA_DIR
    return QString(APP_TESSDATA_DIR);
#else
    return ocr::findTessdataDir(tessPath_);
#endif
}

QString OcrProcessor::renderPageToTempPNG(int pageIndex) {
    if (!pdfDoc_) {
        pdfDoc_ = new QPdfDocument(this);
        if (pdfDoc_->load(pdfPath_) != QPdfDocument::Error::None) {
            throw std::runtime_error("Failed to open PDF");
        }
    }

    QSizeF pageSize = pdfDoc_->pagePointSize(pageIndex);
    
    const double dpi = 300.0;
    double scale = dpi / 72.0;
    int w = static_cast<int>(pageSize.width() * scale);
    int h = static_cast<int>(pageSize.height() * scale);

    QImage image = pdfDoc_->render(pageIndex, QSize(w, h));
    
    if (image.isNull()) {
        throw std::runtime_error("Failed to render PDF page");
    }

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/qt_tess_tmp";
    QDir().mkpath(tempDir);

    QByteArray nameHash = QCryptographicHash::hash(
        QString("%1_%2").arg(pdfPath_).arg(pageIndex).toUtf8(),
        QCryptographicHash::Sha1
    );
    QString fname = QString("%1/page_%2_%3.png")
        .arg(tempDir)
        .arg(pageIndex)
        .arg(QString(nameHash.toHex()).left(8));
    
    if (!image.save(fname, "PNG")) {
        throw std::runtime_error("Failed to save rendered page");
    }
    
    return fname;
}

QString OcrProcessor::runTesseractOnImage(const QString &imagePath, const QString &tessLang, 
                                         const QString &tessdataDir) {
    tesseract::TessBaseAPI api;
    
    const char *datapath = tessdataDir.isEmpty() ? nullptr : tessdataDir.toUtf8().constData();
    
    if (api.Init(datapath, tessLang.toUtf8().constData())) {
        throw std::runtime_error(
            QString("Could not initialize tesseract for lang %1 (datapath=%2)")
            .arg(tessLang, tessdataDir.isEmpty() ? "default" : tessdataDir)
            .toStdString()
        );
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
        delete[] out;
    }
    
    pixDestroy(&image);
    api.End();
    
    return result;
}

QString OcrProcessor::runGoogleVisionOnImage(const QString &imagePath, const QString &visionLang) {
    if (apiKey_.isEmpty() && googleServiceAccountPath_.isEmpty()) {
        throw std::runtime_error("Google Vision requires an API key or a service account JSON file.");
    }
    
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Failed to open rendered image for Vision.");
    }
    QByteArray bytes = f.readAll();
    f.close();
    
    QString base64 = QString::fromLatin1(bytes.toBase64());

    QJsonObject imageObj;
    imageObj["content"] = base64;

    QJsonObject feature;
    feature["type"] = "DOCUMENT_TEXT_DETECTION";

    QJsonArray features;
    features.append(feature);

    QJsonObject imageContext;
    QJsonArray langHints;
    langHints.append(visionLang);
    imageContext["languageHints"] = langHints;

    QJsonObject request;
    request["image"] = imageObj;
    request["features"] = features;
    request["imageContext"] = imageContext;

    QJsonArray requests;
    requests.append(request);
    
    QJsonObject payload;
    payload["requests"] = requests;

    QNetworkRequest netReq;
    if (!apiKey_.isEmpty()) {
        netReq.setUrl(QUrl(QString("https://vision.googleapis.com/v1/images:annotate?key=%1").arg(apiKey_)));
    } else {
        QString token = getAccessTokenFromServiceAccount(googleServiceAccountPath_);
        netReq.setUrl(QUrl("https://vision.googleapis.com/v1/images:annotate"));
        netReq.setRawHeader("Authorization", QString("Bearer %1").arg(token).toUtf8());
    }
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
    if (!doc.isObject()) {
        throw std::runtime_error("Invalid response from Google Vision.");
    }
    
    QJsonObject root = doc.object();
    QJsonArray responses = root["responses"].toArray();
    if (responses.isEmpty()) {
        return QString();
    }

    QJsonObject first = responses[0].toObject();
    QString fullText = first["fullTextAnnotation"].toObject()["text"].toString();
    
    return fullText;
}

QStringList OcrProcessor::splitTextIntoBatches(const QString &text, int wordsPerBatch) {
    // QRegExp was removed in Qt6; use QRegularExpression which is the modern API.
    QStringList words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList batches;
    
    for (int i = 0; i < words.size(); i += wordsPerBatch) {
        int count = qMin(wordsPerBatch, words.size() - i);
        batches.append(words.mid(i, count).join(" "));
    }
    
    return batches;
}

QString OcrProcessor::callLLM(const QString &textChunk, const QString &batchInfo) {
    if (apiKey_.isEmpty()) {
        throw std::runtime_error("LLM API key required.");
    }

    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = "You are an expert assistant.";

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = QString("%1\n\nPlease process the following text content %2:\n\n---\n%3\n---")
        .arg(prompt_, batchInfo, textChunk);

    QJsonArray messages;
    messages.append(systemMsg);
    messages.append(userMsg);

    QJsonObject payload;
    
    // Parse provider and model
    QString provider, model;
    if (llmProvider_.contains(":")) {
        QStringList parts = llmProvider_.split(":", Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            provider = parts[0].trimmed();
            model = parts[1].trimmed();
        }
    } else {
        provider = "OpenAI";
        model = "gpt-4o";
    }

    payload["model"] = model;
    payload["messages"] = messages;

    QNetworkRequest req;
    
    if (provider == "OpenAI") {
        req.setUrl(QUrl("https://api.openai.com/v1/chat/completions"));
        req.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey_).toUtf8());
    } else if (provider == "OpenRouter") {
        req.setUrl(QUrl("https://openrouter.ai/api/v1/chat/completions"));
        req.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey_).toUtf8());
    } else {
        throw std::runtime_error("Unsupported LLM provider.");
    }
    
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
    if (!doc.isObject()) {
        throw std::runtime_error("Invalid response from LLM API.");
    }
    
    QJsonObject root = doc.object();
    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        return QString();
    }
    
    QString text = choices[0].toObject()["message"].toObject()["content"].toString();
    return text;
}

void OcrProcessor::workerRoutine() {
    try {
        emitProgress("Loading PDF...", 2);
        
        QPdfDocument doc;
        if (doc.load(pdfPath_) != QPdfDocument::Error::None) {
            throw std::runtime_error("Failed to open PDF");
        }
        
        int totalPages = doc.pageCount();
        int s = (startPage_ >= 1) ? startPage_ : 1;
        int e = (endPage_ >= 1) ? endPage_ : totalPages;
        
        if (s < 1) s = 1;
        if (e > totalPages) e = totalPages;
        if (s > e) {
            throw std::runtime_error("Invalid page range");
        }

        // Render pages
        QStringList images;
        int pageCount = e - s + 1;
        
        for (int i = s - 1; i < e; ++i) {
            if (stopFlag_.load()) {
                throw std::runtime_error("Process stopped by user.");
            }
            
            double progress = 5 + ((i - (s - 1) + 1.0) / pageCount) * 15;
            emitProgress(QString("Rendering page %1/%2...").arg(i - (s - 1) + 1).arg(pageCount), progress);
            
            QString png = renderPageToTempPNG(i);
            images.append(png);
        }

        if (stopFlag_.load()) {
            throw std::runtime_error("Process stopped by user.");
        }

        // Perform OCR
        emitProgress("Performing OCR...", 20);
        
        QStringList ocrResults;
        auto langPair = langMap_.value(langKey_, qMakePair(QString("eng"), QString("en")));
        QString tessLang = langPair.first;
        QString visionLang = langPair.second;
        QString tessdataDir = getTessdataDir();

        for (int i = 0; i < images.size(); ++i) {
            if (stopFlag_.load()) {
                throw std::runtime_error("Process stopped by user.");
            }
            
            double progress = 20 + ((i + 1.0) / images.size()) * 30;
            emitProgress(QString("OCR page %1/%2...").arg(i + 1).arg(images.size()), progress);
            
            QString imagePath = images[i];
            QString text;
            
            if (ocrEngine_ == "Tesseract") {
                text = runTesseractOnImage(imagePath, tessLang, tessdataDir);
            } else if (ocrEngine_ == "Google Vision") {
                text = runGoogleVisionOnImage(imagePath, visionLang);
            } else {
                throw std::runtime_error("Unknown OCR engine");
            }
            
            ocrResults << text;
            
            // Clean up temp file
            QFile::remove(imagePath);
        }

        if (stopFlag_.load()) {
            throw std::runtime_error("Process stopped by user.");
        }

        QString fullText = ocrResults.join("\n\n");

        // If OCR only, save and finish
        if (ocrOnly_ || prompt_.isEmpty()) {
            QFile outf(outputPath_);
            if (!outf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                throw std::runtime_error("Failed to open output file for writing.");
            }
            outf.write(fullText.toUtf8());
            outf.close();
            
            emitProgress("Done", 100);
            emit finished(outputPath_);
            
            // Move back to main thread
            this->moveToThread(QCoreApplication::instance()->thread());
            return;
        }

        // LLM processing
        emitProgress("Splitting text into batches...", 55);
        QStringList batches = splitTextIntoBatches(fullText);
        QStringList llmOut;
        
        for (int i = 0; i < batches.size(); ++i) {
            if (stopFlag_.load()) {
                throw std::runtime_error("Process stopped by user.");
            }
            
            double progress = 60 + ((i + 1.0) / batches.size()) * 35;
            emitProgress(QString("Calling LLM (batch %1/%2)").arg(i + 1).arg(batches.size()), progress);
            
            QString batchInfo = QString("(Batch %1 of %2)").arg(i + 1).arg(batches.size());
            QString res = callLLM(batches[i], batchInfo);
            llmOut << res;
        }

        QString finalOutput = llmOut.join("\n\n---\n\n");
        
        QFile outf(outputPath_);
        if (!outf.open(QIODevice::WriteOnly | QIODevice::Text)) {
            throw std::runtime_error("Failed to open output file for writing.");
        }
        outf.write(finalOutput.toUtf8());
        outf.close();

        emitProgress("Done", 100);
        emit finished(outputPath_);
        
    } catch (const std::exception &ex) {
        emit errorOccurred(QString::fromStdString(ex.what()));
    } catch (...) {
        emit errorOccurred("Unknown error during processing.");
    }
    
    // Move back to main thread
    this->moveToThread(QCoreApplication::instance()->thread());
}