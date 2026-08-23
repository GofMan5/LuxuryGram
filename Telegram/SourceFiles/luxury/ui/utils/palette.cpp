// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
//
// Code is based on:
// - https://github.com/androidx/androidx/blob/androidx-main/palette/palette/src/main/java/androidx/palette/graphics/Palette.java
// - https://github.com/androidx/androidx/blob/androidx-main/palette/palette/src/main/java/androidx/palette/graphics/Target.java
#include "luxury/ui/utils/palette.h"

#include "color_cut_quantizer.h"
#include "color_utils.h"

#include <cmath>

namespace Luxury::Ui {
namespace {

// Black, white and the flesh tones just off the red I-line are never a useful
// accent, so they are dropped before quantizing.
bool UsefulColor(QRgb rgb, const std::array<float, 3> &hsl) {
	const auto isBlack = (hsl[2] <= 0.05f);
	const auto isWhite = (hsl[2] >= 0.95f);
	const auto isNearRedILine = (hsl[0] >= 10.0f
		&& hsl[0] <= 37.0f
		&& hsl[1] <= 0.82f);
	return !isWhite && !isBlack && !isNearRedILine;
}

std::vector<QRgb> PixelsFromImage(const QImage &image) {
	const auto img = image.convertToFormat(QImage::Format_ARGB32);

	std::vector<QRgb> pixels;
	pixels.reserve(img.width() * img.height());
	for (auto y = 0; y != img.height(); ++y) {
		const auto line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		pixels.insert(pixels.end(), line, line + img.width());
	}
	return pixels;
}

QImage ScaleBitmapDown(const QImage &image) {
	const auto area = image.width() * image.height();
	if (area <= Palette::DEFAULT_RESIZE_BITMAP_AREA) {
		return image;
	}
	const auto scale = std::sqrt(
		double(Palette::DEFAULT_RESIZE_BITMAP_AREA) / area);
	return image.scaled(
		int(std::ceil(image.width() * scale)),
		int(std::ceil(image.height() * scale)),
		Qt::IgnoreAspectRatio,
		Qt::FastTransformation);
}

} // namespace

Swatch::Swatch(QRgb color, int population)
	: _red(qRed(color))
	  , _green(qGreen(color))
	  , _blue(qBlue(color))
	  , _rgb(color)
	  , _population(population) {
}

QRgb Swatch::rgb() const {
	return _rgb;
}

int Swatch::population() const {
	return _population;
}

std::array<float, 3> Swatch::hsl() const {
	if (!_hslCalculated) {
		_hsl = ColorUtils::RGBToHSL(_red, _green, _blue);
		_hslCalculated = true;
	}
	return _hsl;
}

const Target Target::LIGHT_VIBRANT = []()
{
	Target t;
	setDefaultLightLightnessValues(t);
	setDefaultVibrantSaturationValues(t);
	return t;
}();

const Target Target::VIBRANT = []()
{
	Target t;
	setDefaultNormalLightnessValues(t);
	setDefaultVibrantSaturationValues(t);
	return t;
}();

const Target Target::DARK_VIBRANT = []()
{
	Target t;
	setDefaultDarkLightnessValues(t);
	setDefaultVibrantSaturationValues(t);
	return t;
}();

const Target Target::LIGHT_MUTED = []()
{
	Target t;
	setDefaultLightLightnessValues(t);
	setDefaultMutedSaturationValues(t);
	return t;
}();

const Target Target::MUTED = []()
{
	Target t;
	setDefaultNormalLightnessValues(t);
	setDefaultMutedSaturationValues(t);
	return t;
}();

const Target Target::DARK_MUTED = []()
{
	Target t;
	setDefaultDarkLightnessValues(t);
	setDefaultMutedSaturationValues(t);
	return t;
}();

Target::Target() {
	setTargetDefaultValues(_saturationTargets);
	setTargetDefaultValues(_lightnessTargets);
	setDefaultWeights(*this);
}

bool Target::operator==(const Target &other) const {
	for (int i = 0; i < 3; ++i) {
		if (_saturationTargets[i] != other._saturationTargets[i]) return false;
		if (_lightnessTargets[i] != other._lightnessTargets[i]) return false;
		if (_weights[i] != other._weights[i]) return false;
	}
	return _isExclusive == other._isExclusive;
}

float Target::minimumSaturation() const {
	return _saturationTargets[0];
}

float Target::targetSaturation() const {
	return _saturationTargets[1];
}

float Target::maximumSaturation() const {
	return _saturationTargets[2];
}

float Target::minimumLightness() const {
	return _lightnessTargets[0];
}

float Target::targetLightness() const {
	return _lightnessTargets[1];
}

float Target::maximumLightness() const {
	return _lightnessTargets[2];
}

float Target::saturationWeight() const {
	return _weights[0];
}

float Target::lightnessWeight() const {
	return _weights[1];
}

float Target::populationWeight() const {
	return _weights[2];
}

bool Target::isExclusive() const {
	return _isExclusive;
}

void Target::normalizeWeights() {
	float sum = 0.0f;
	for (int i = 0; i < _weights.size(); i++) {
		float weight = _weights[i];
		if (weight > 0) {
			sum += weight;
		}
	}
	if (sum != 0.0f) {
		for (int i = 0; i < _weights.size(); i++) {
			if (_weights[i] > 0) {
				_weights[i] /= sum;
			}
		}
	}
}

void Target::setDefaultLightLightnessValues(Target &target) {
	target._lightnessTargets[0] = 0.55f;
	target._lightnessTargets[1] = 0.74f;
}

void Target::setDefaultNormalLightnessValues(Target &target) {
	target._lightnessTargets[0] = 0.3f;
	target._lightnessTargets[1] = 0.5f;
	target._lightnessTargets[2] = 0.7f;
}

void Target::setDefaultDarkLightnessValues(Target &target) {
	target._lightnessTargets[1] = 0.26f;
	target._lightnessTargets[2] = 0.45f;
}

void Target::setDefaultVibrantSaturationValues(Target &target) {
	target._saturationTargets[0] = 0.35f;
	target._saturationTargets[1] = 1.0f;
}

void Target::setDefaultMutedSaturationValues(Target &target) {
	target._saturationTargets[1] = 0.3f;
	target._saturationTargets[2] = 0.4f;
}

void Target::setTargetDefaultValues(std::array<float, 3> &values) {
	values[0] = 0.0f;
	values[1] = 0.5f;
	values[2] = 1.0f;
}

void Target::setDefaultWeights(Target &target) {
	target._weights[0] = 0.24f;
	target._weights[1] = 0.52f;
	target._weights[2] = 0.24f;
}

Palette::Palette(
	std::vector<Swatch> swatches,
	std::vector<Target> targets)
	: _swatches(std::move(swatches))
	  , _targets(std::move(targets)) {
}

const Swatch *Palette::darkVibrantSwatch() const {
	return swatchForTarget(Target::DARK_VIBRANT);
}

const Swatch *Palette::mutedSwatch() const {
	return swatchForTarget(Target::MUTED);
}

const Swatch *Palette::darkMutedSwatch() const {
	return swatchForTarget(Target::DARK_MUTED);
}

const Swatch *Palette::dominantSwatch() const {
	return _dominantSwatch;
}

const Swatch *Palette::swatchForTarget(const Target &target) const {
	for (const auto &[key, swatch] : _selectedSwatches) {
		if (key == target) {
			return swatch;
		}
	}
	return nullptr;
}

void Palette::generate() {
	_selectedSwatches.clear();
	_dominantSwatch = findDominantSwatch();

	for (auto &target : _targets) {
		target.normalizeWeights();
		const auto swatch = generateScoredTarget(target);
		_selectedSwatches.push_back({ target, swatch });
	}

	_usedColors.clear();
}

const Swatch *Palette::generateScoredTarget(const Target &target) {
	const auto maxScoreSwatch = getMaxScoredSwatchForTarget(target);
	if (maxScoreSwatch && target.isExclusive()) {
		_usedColors.insert(maxScoreSwatch->rgb());
	}
	return maxScoreSwatch;
}

const Swatch *Palette::getMaxScoredSwatchForTarget(const Target &target) {
	float maxScore = 0.0f;
	const Swatch *maxScoreSwatch = nullptr;

	for (const auto &swatch : _swatches) {
		if (shouldBeScoredForTarget(swatch, target)) {
			const auto score = generateScore(swatch, target);
			if (!maxScoreSwatch || score > maxScore) {
				maxScoreSwatch = &swatch;
				maxScore = score;
			}
		}
	}

	return maxScoreSwatch;
}

bool Palette::shouldBeScoredForTarget(const Swatch &swatch, const Target &target) {
	const auto hsl = swatch.hsl();
	return hsl[1] >= target.minimumSaturation()
		&& hsl[1] <= target.maximumSaturation()
		&& hsl[2] >= target.minimumLightness()
		&& hsl[2] <= target.maximumLightness()
		&& _usedColors.find(swatch.rgb()) == _usedColors.end();
}

float Palette::generateScore(const Swatch &swatch, const Target &target) {
	const auto hsl = swatch.hsl();

	float saturationScore = 0.0f;
	float luminanceScore = 0.0f;
	float populationScore = 0.0f;

	const auto maxPopulation = _dominantSwatch ? _dominantSwatch->population() : 1;

	if (target.saturationWeight() > 0) {
		saturationScore = target.saturationWeight()
			* (1.0f - std::abs(hsl[1] - target.targetSaturation()));
	}
	if (target.lightnessWeight() > 0) {
		luminanceScore = target.lightnessWeight()
			* (1.0f - std::abs(hsl[2] - target.targetLightness()));
	}
	if (target.populationWeight() > 0) {
		populationScore = target.populationWeight()
			* (static_cast<float>(swatch.population()) / static_cast<float>(maxPopulation));
	}

	return saturationScore + luminanceScore + populationScore;
}

const Swatch *Palette::findDominantSwatch() {
	int maxPop = 0;
	const Swatch *maxSwatch = nullptr;

	for (const auto &swatch : _swatches) {
		if (swatch.population() > maxPop) {
			maxSwatch = &swatch;
			maxPop = swatch.population();
		}
	}

	return maxSwatch;
}

Palette::Builder Palette::from(const QImage &image) {
	return Builder(image);
}

Palette::Builder::Builder(const QImage &image)
	: _image(image) {
	// Order matters: each target claims its colour and takes it out of the
	// running for the ones after it.
	_targets.push_back(Target::LIGHT_VIBRANT);
	_targets.push_back(Target::VIBRANT);
	_targets.push_back(Target::DARK_VIBRANT);
	_targets.push_back(Target::LIGHT_MUTED);
	_targets.push_back(Target::MUTED);
	_targets.push_back(Target::DARK_MUTED);
}

Palette Palette::Builder::generate() {
	auto quantizer = ColorCutQuantizer(
		PixelsFromImage(ScaleBitmapDown(_image)),
		DEFAULT_CALCULATE_NUMBER_COLORS,
		UsefulColor);

	auto palette = Palette(quantizer.quantizedColors(), _targets);
	palette.generate();

	return palette;
}

} // namespace Luxury::Ui
