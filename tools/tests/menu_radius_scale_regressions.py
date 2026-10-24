"""Scale the Menu Corner Radius setting with the interface like the style it replaces.

The slider picks 2-18 px at 100%; popup menus at other scales must get the
same value scaled, as the default style radius they override is.
"""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source

SETTINGS = 'Telegram/lib_ui/luxury/luxury_ui_settings.cpp'


def check(root):
    helper = function(source(SETTINGS), 'int effectiveMenuRadius(int styleRadius)')
    cpp(root, 'menu_radius_scale', r'''
#include <cassert>
#include <cmath>
#include <iostream>

int g_scale = 100;
int g_value = 10;

namespace style {
template <typename T>
T ConvertScale(T value, int scale) {
	const auto result = T(std::round(double(value) * scale / 100. - 0.01));
	return (!value || result) ? result : 1;
}
template <typename T>
T ConvertScale(T value) { return ConvertScale(value, g_scale); }
} // namespace style

namespace LuxuryUiSettings {
constexpr int kMaxMenuRadius = 18;
constexpr int kMinMenuRadius = 2;
constexpr int kDefaultMenuRadius = 10;
int getMenuRadius() { return g_value; }

''' + helper + r'''

} // namespace LuxuryUiSettings

int main() {
	for (const auto scale : { 100, 125, 150, 200, 300 }) {
		g_scale = scale;
		const auto styleRadius = style::ConvertScale(10);
		for (auto value = LuxuryUiSettings::kMinMenuRadius;
				value <= LuxuryUiSettings::kMaxMenuRadius;
				++value) {
			g_value = value;
			const auto radius = LuxuryUiSettings::effectiveMenuRadius(
				styleRadius);
			if (radius != style::ConvertScale(value)) {
				std::cerr
					<< "scale " << scale << " value " << value
					<< ": menu radius " << radius
					<< ", expected " << style::ConvertScale(value) << "\n";
				assert(!"the menu radius ignores the interface scale");
			}
		}
		g_value = LuxuryUiSettings::kDefaultMenuRadius;
		assert(LuxuryUiSettings::effectiveMenuRadius(styleRadius)
			== styleRadius);
	}
	std::cout << "LuxuryGram menu radius scale PASS: the slider value follows the interface scale like the default style radius\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='menu-radius-scale-') as directory:
        check(Path(directory))
