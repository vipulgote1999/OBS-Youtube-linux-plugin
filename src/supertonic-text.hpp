#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

/**
 * Text preparation for the Supertonic frontend.
 *
 * Supertonic owns no phonemizer: it consumes language-tagged text and maps
 * Unicode code points through a shipped indexer.  These helpers mirror the
 * archived C++ reference implementation but use Qt's Unicode tables instead
 * of hand-written NFKD tables.
 */
namespace SupertonicText {

/** Replace symbols, remove emoji, fix spacing and add language tags. */
QString preprocess(const QString &text, const QString &lang);

/** Split into sentence-sized chunks that never exceed maxLen characters. */
QStringList chunk(const QString &text, int maxLen = 300);

/** NFKD-normalize and return UTF-16 code units for the model indexer. */
std::vector<uint16_t> unicodeValues(const QString &text);

} // namespace SupertonicText
