#pragma once
#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>
#include <QNetworkAccessManager>
#include <QPdfDocument>

struct ProgressUpdate { QString status; double percent; };

class OcrProcessor : public QObject {
    Q_OBJECT
public:
    explicit OcrProcessor(QObject *parent = nullptr);
    ~OcrProcessor();

    Q_INVOKABLE void selectPdf(const QString &path);
    Q_INVOKABLE void selectOutput(const QString &path);
    Q_INVOKABLE void setTesseractPath(const QString &path);
    Q_INVOKABLE void setOcrEngine(const QString &engine); // "Tesseract" or "Google Vision"
    Q_INVOKABLE void setLanguage(const QString &langKey); // e.g. "Kannada (kan)"
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void setPrompt(const QString &p);
    Q_INVOKABLE void setPageRange(int start, int end);
    Q_INVOKABLE void startProcessing();
    Q_INVOKABLE void stopProcessing();
    Q_INVOKABLE QStringList languageOptions() const;

signals:
    void progressChanged(QString status, double percent);
    void finished(QString outPath);
    void errorOccurred(QString msg);

private:
    // worker thread
    void workerRoutine();

    // helpers
    QString pdfPath_;
    QString outputPath_;
    QString tessPath_;
    QString ocrEngine_;
    QString langKey_;
    QString apiKey_;
    QString prompt_;
    int startPage_, endPage_;
    std::atomic<bool> stopFlag_;
    QNetworkAccessManager *netman_;
    QThread workerThread_;

    // map visible language -> pair(tess, vision)
    QMap<QString, QPair<QString, QString>> langMap_;

    // PDF document instance
    QPdfDocument *pdfDoc_;

    // file helpers
    QString renderPageToTempPNG(int pageIndex);
    QString runTesseractOnImage(const QString &imagePath, const QString &tessLang, const QString &tessdataDir);
    QString runGoogleVisionOnImage(const QString &imagePath, const QString &visionLang);
    QString callLLM(const QString &textChunk, const QString &batchInfo);
    QStringList splitTextIntoBatches(const QString &text, int wordsPerBatch = 1100);

    // emit progress convenience
    void emitProgress(const QString &s, double p) { emit progressChanged(s, p); }
};
