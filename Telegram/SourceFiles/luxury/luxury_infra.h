// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace LuxuryInfra {

void init();

// Called from ~Application, after every session is gone. Drains the database
// queue: rows are posted and forgotten, so the last batch before a quit is lost
// without this, and a pass of the queue could outlive what it writes through.
void finish();

}
