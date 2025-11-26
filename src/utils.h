#pragma once

#include <QString>

namespace ocr {
// Given the path to a tesseract executable, attempt to locate a tessdata directory
// using the same heuristics used by OcrProcessor::getTessdataDir(). Returns empty
// string if not found.
QString findTessdataDir(const QString &tessExecutablePath);

} // namespace ocr
