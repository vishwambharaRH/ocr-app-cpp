#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QPair>
#include <QThread>
#include <atomic>
#include <QNetworkAccessManager>
#include <QPdfDocument>

class OcrProcessor : public QObject {
    Q_OBJECT
public:
    explicit OcrProcessor(QObject *parent = nullptr);
    ~OcrProcessor();

    Q_INVOKABLE void selectPdf(const QString &path);
    Q_INVOKABLE void selectOutput(const QString &path);
    Q_INVOKABLE void setTesseractPath(const QString &path);
    Q_INVOKABLE void setOcrEngine(const QString &engine);
    Q_INVOKABLE void setLanguage(const QString &langKey);
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void setGoogleServiceAccountPath(const QString &path);
    Q_INVOKABLE void setPrompt(const QString &p);
    Q_INVOKABLE void setPageRange(int start, int end);
    Q_INVOKABLE void setOcrOnly(bool ocrOnly);
    Q_INVOKABLE void setLlmProvider(const QString &provider);
    Q_INVOKABLE void startProcessing();
    Q_INVOKABLE void stopProcessing();
    Q_INVOKABLE QStringList languageOptions() const;

signals:
    void progressChanged(QString status, double percent);
    void finished(QString outPath);
    void errorOccurred(QString msg);
    // Emitted when the background worker/thread has fully stopped and cleaned up
    void stopped();

private slots:
    void workerRoutine();

private:
    // Configuration
    QString pdfPath_;
    QString outputPath_;
    QString tessPath_;
    QString ocrEngine_;
    QString langKey_;
    QString apiKey_;
    QString prompt_;
    QString llmProvider_;
    int startPage_;
    int endPage_;
    bool ocrOnly_;
    std::atomic<bool> stopFlag_;
    
    // Threading
    QThread *workerThread_;
    QNetworkAccessManager *netman_;

    // Language mapping
    QMap<QString, QPair<QString, QString>> langMap_;

    // PDF handling
    QPdfDocument *pdfDoc_;

    // Helper methods
    QString renderPageToTempPNG(int pageIndex);
    QString runTesseractOnImage(const QString &imagePath, const QString &tessLang, const QString &tessdataDir);
    QString runGoogleVisionOnImage(const QString &imagePath, const QString &visionLang);
    // Google service account auth
    QString getAccessTokenFromServiceAccount(const QString &jsonPath);
    QString googleServiceAccountPath_;
    QString googleAccessToken_;
    qint64 googleAccessTokenExpiry_ = 0; // unix epoch seconds
    QString callLLM(const QString &textChunk, const QString &batchInfo);
    QStringList splitTextIntoBatches(const QString &text, int wordsPerBatch = 1100);
    QString getTessdataDir();

    void emitProgress(const QString &s, double p) { 
        emit progressChanged(s, p); 
    }
};