#include "utils.h"
#include <QFileInfo>
#include <QDir>

namespace ocr {

QString findTessdataDir(const QString &tessExecutablePath) {
    if (tessExecutablePath.isEmpty()) return QString();

    QFileInfo fi(tessExecutablePath);
    QString binDir = fi.absolutePath();
    QDir d(binDir);

    // Try parent/share/tessdata (Homebrew style)
    d.cdUp();
    if (d.exists("share/tessdata")) {
        return d.absoluteFilePath("share/tessdata");
    }

    // Try same directory as executable (Windows style)
    if (QDir(binDir).exists("tessdata")) {
        return QDir(binDir).absoluteFilePath("tessdata");
    }

    // Try /usr/share/tesseract-ocr/*/tessdata (Linux style)
    QDir usrShare("/usr/share");
    if (usrShare.exists()) {
        QStringList tessDirs = usrShare.entryList(QStringList() << "tesseract-ocr", QDir::Dirs);
        for (const QString &dir : tessDirs) {
            QDir tessDir = usrShare;
            tessDir.cd(dir);
            QStringList versionDirs = tessDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &ver : versionDirs) {
                QDir verDir = tessDir;
                verDir.cd(ver);
                if (verDir.exists("tessdata")) {
                    return verDir.absoluteFilePath("tessdata");
                }
            }
            // if not found, continue searching
        }
    }

    return QString();
}

} // namespace ocr
