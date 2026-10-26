/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style_radius.h"
#include "ui/chat/chat_style.h"
#include "base/options.h"

#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

constexpr auto kBubbleRadiusSliderMax = 16;

int AppliedBubbleRadius = 16;
int BubbleRadiusOverride = -1;

[[nodiscard]] int ClampBubbleRadiusValue(int value) {
	return (value < 0)
		? 0
		: (value > kBubbleRadiusSliderMax)
		? kBubbleRadiusSliderMax
		: value;
}

[[nodiscard]] int EffectiveBubbleRadiusValue() {
	return (BubbleRadiusOverride >= 0)
		? BubbleRadiusOverride
		: AppliedBubbleRadius;
}

[[nodiscard]] int MapBubbleRadius(int sliderValue, int maximum) {
	if (sliderValue <= 0 || maximum <= 0) {
		return 0;
	} else if (sliderValue >= kBubbleRadiusSliderMax) {
		return maximum;
	}
	return (sliderValue * maximum + (kBubbleRadiusSliderMax / 2))
		/ kBubbleRadiusSliderMax;
}

base::options::toggle UseSmallMsgBubbleRadius({
	.id = kOptionUseSmallMsgBubbleRadius,
	.name = "Use small message bubble radius",
	.description = "Makes most message bubbles square-ish.",
	.restartRequired = true,
});

} // namespace

const char kOptionUseSmallMsgBubbleRadius[] = "use-small-msg-bubble-radius";

void SetAppliedBubbleRadius(int value) {
	AppliedBubbleRadius = ClampBubbleRadiusValue(value);
}

void SetBubbleRadiusOverride(int value) {
	BubbleRadiusOverride = ClampBubbleRadiusValue(value);
}

void ClearBubbleRadiusOverride() {
	BubbleRadiusOverride = -1;
}

int BubbleRadiusSmall() {
	return MapBubbleRadius(EffectiveBubbleRadiusValue(), st::bubbleRadiusSmall);
}

int BubbleRadiusLarge() {
	return MapBubbleRadius(
		EffectiveBubbleRadiusValue(),
		(UseSmallMsgBubbleRadius.value()
			? st::bubbleRadiusSmall
			: st::bubbleRadiusLarge));
}

int BubbleCornerRadius(BubbleCornerRounding corner) {
	switch (corner) {
	case BubbleCornerRounding::None:
	case BubbleCornerRounding::Tail: return 0;
	case BubbleCornerRounding::Small: return BubbleRadiusSmall();
	case BubbleCornerRounding::Large: return BubbleRadiusLarge();
	}
	Unexpected("Corner in BubbleCornerRadius.");
}

int MsgFileThumbRadiusSmall() {
	return MapBubbleRadius(
		EffectiveBubbleRadiusValue(),
		st::msgFileThumbRadiusSmall);
}

int MsgFileThumbRadiusLarge() {
	return MapBubbleRadius(
		EffectiveBubbleRadiusValue(),
		(UseSmallMsgBubbleRadius.value()
			? st::msgFileThumbRadiusSmall
			: st::msgFileThumbRadiusLarge));
}

}
