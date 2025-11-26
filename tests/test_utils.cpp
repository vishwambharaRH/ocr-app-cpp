#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "utils.h"

using namespace ocr;

TEST(UtilsTest, EmptyPathReturnsEmpty) {
    EXPECT_TRUE(findTessdataDir("").isEmpty());
}

TEST(UtilsTest, FindsHomebrewStyleShareTessdata) {
    // Create a temporary directory with structure: /tmp/xyz/share/tessdata and a bin/tesseract
    QDir tmp = QDir::temp();
    QTemporaryDir td;
    ASSERT_TRUE(td.isValid());
    QString basePath = td.path();

    QDir dir(basePath);
    dir.mkpath("share/tessdata");
    dir.mkpath("bin");
    QFile f(dir.absoluteFilePath("bin/tesseract"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.close();
    // Make executable
    QFile::setPermissions(f.fileName(), QFile::permissions(f.fileName()) | QFileDevice::ExeUser);

    QString found = findTessdataDir(dir.absoluteFilePath("bin/tesseract"));
    EXPECT_FALSE(found.isEmpty());
    EXPECT_TRUE(found.endsWith("share/tessdata"));
}
