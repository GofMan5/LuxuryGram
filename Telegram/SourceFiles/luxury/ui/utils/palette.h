// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <array>
#include <set>
#include <utility>
#include <vector>
#include <QImage>

namespace Luxury::Ui {

class Swatch
{
public:
	Swatch(QRgb color, int population);

	[[nodiscard]] QRgb rgb() const;
	[[nodiscard]] int population() const;
	[[nodiscard]] std::array<float, 3> hsl() const;

private:
	int _red = 0;
	int _green = 0;
	int _blue = 0;
	QRgb _rgb = 0;
	int _population = 0;

	mutable std::array<float, 3> _hsl = {};
	mutable bool _hslCalculated = false;
};

// One of the six ranges of saturation and lightness a swatch is scored against.
// All six stay in play even though only three are ever asked for: they are
// exclusive, so each claims its colour and takes it out of the running for the
// ones after it. Dropping any changes what the rest pick.
class Target
{
public:
	static const Target LIGHT_VIBRANT;
	static const Target VIBRANT;
	static const Target DARK_VIBRANT;
	static const Target LIGHT_MUTED;
	static const Target MUTED;
	static const Target DARK_MUTED;

	Target();

	[[nodiscard]] float minimumSaturation() const;
	[[nodiscard]] float targetSaturation() const;
	[[nodiscard]] float maximumSaturation() const;
	[[nodiscard]] float minimumLightness() const;
	[[nodiscard]] float targetLightness() const;
	[[nodiscard]] float maximumLightness() const;
	[[nodiscard]] float saturationWeight() const;
	[[nodiscard]] float lightnessWeight() const;
	[[nodiscard]] float populationWeight() const;
	[[nodiscard]] bool isExclusive() const;

	void normalizeWeights();

	bool operator==(const Target &other) const;

private:
	static void setDefaultLightLightnessValues(Target &target);
	static void setDefaultNormalLightnessValues(Target &target);
	static void setDefaultDarkLightnessValues(Target &target);
	static void setDefaultVibrantSaturationValues(Target &target);
	static void setDefaultMutedSaturationValues(Target &target);
	static void setTargetDefaultValues(std::array<float, 3> &values);
	static void setDefaultWeights(Target &target);

	std::array<float, 3> _saturationTargets;
	std::array<float, 3> _lightnessTargets;
	std::array<float, 3> _weights;
	bool _isExclusive = true;
};

class Palette
{
public:
	class Builder;

	[[nodiscard]] const Swatch *darkVibrantSwatch() const;
	[[nodiscard]] const Swatch *mutedSwatch() const;
	[[nodiscard]] const Swatch *darkMutedSwatch() const;
	[[nodiscard]] const Swatch *dominantSwatch() const;

	[[nodiscard]] static Builder from(const QImage &image);

	static constexpr int DEFAULT_RESIZE_BITMAP_AREA = 112 * 112;
	static constexpr int DEFAULT_CALCULATE_NUMBER_COLORS = 16;

private:
	Palette(
		std::vector<Swatch> swatches,
		std::vector<Target> targets);

	void generate();
	[[nodiscard]] const Swatch *swatchForTarget(const Target &target) const;
	const Swatch *generateScoredTarget(const Target &target);
	const Swatch *getMaxScoredSwatchForTarget(const Target &target);
	bool shouldBeScoredForTarget(const Swatch &swatch, const Target &target);
	float generateScore(const Swatch &swatch, const Target &target);
	const Swatch *findDominantSwatch();

	std::vector<Swatch> _swatches;
	std::vector<Target> _targets;
	std::vector<std::pair<Target, const Swatch*>> _selectedSwatches;
	std::set<QRgb> _usedColors;
	const Swatch *_dominantSwatch = nullptr;

	friend class Builder;
};

class Palette::Builder
{
public:
	explicit Builder(const QImage &image);

	[[nodiscard]] Palette generate();

private:
	QImage _image;
	std::vector<Target> _targets;
};

} // namespace Luxury::Ui
