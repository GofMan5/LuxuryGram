// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "luxury/features/message_shot/message_shot_render.h"
#include "luxury/features/message_shot/message_shot_theme_state.h"
#include "history/view/history_view_list_widget.h"
#include "ui/chat/chat_style.h"
#include "window/window_session_controller.h"

class HistoryInner;

namespace LuxuryFeatures::MessageShot {

struct ShotConfig
{
	not_null<Window::SessionController*> controller;
	std::shared_ptr<Ui::ChatStyle> st;
	// Ids, not pointers: the box outlives the messages it renders, and it
	// re-renders from this list on every preference toggle.
	std::vector<FullMsgId> messages;
};

// Skips the ones deleted since the box was opened. Empty means nothing is left
// to render.
[[nodiscard]] std::vector<not_null<HistoryItem*>> ResolveMessages(
	const ShotConfig &config);

enum RenderPart
{
	Date,
	Reactions,
	HeaderDecorations,
};

bool ignoreRender(RenderPart part);

bool isChoosingTheme();
bool setChoosingTheme(bool val);

// util
QColor makeDefaultBackgroundColor();

void Make(not_null<QWidget*> box, const ShotConfig &config, const Fn<void(QImage&,bool)>& callback);

void Wrapper(not_null<HistoryView::ListWidget*> widget, Fn<void()> clearSelected);
void Wrapper(not_null<HistoryInner*> widget, Fn<void()> clearSelected);

}
