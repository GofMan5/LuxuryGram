// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtGui/QImage>

namespace Luxury::Ui::Itunes {

QImage FetchCover(const QString &performer,
                   const QString &title,
                   int sizeHintPx = 300,
                   int timeoutMs = 5000);

}
