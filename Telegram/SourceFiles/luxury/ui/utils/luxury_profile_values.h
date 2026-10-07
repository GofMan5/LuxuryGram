// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "rpl/producer.h"
#include "ui/text/text_entity.h"

class PeerData;

QString IDString(not_null<PeerData*> peer);
QString IDString(MsgId topicRootId);

rpl::producer<TextWithEntities> IDValue(not_null<PeerData*> peer);
rpl::producer<TextWithEntities> IDValue(MsgId topicRootId);