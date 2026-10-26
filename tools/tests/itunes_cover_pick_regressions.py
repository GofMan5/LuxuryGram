"""Saved music covers never show another artist's artwork.

pickArtworkUrl fell back to the first iTunes search hit whenever no result
matched both the title and an artist, so an unknown track got the cover of
whatever song iTunes ranked first. Compiles the real function on doubles.
"""
from pathlib import Path
import tempfile
from library_regressions import cpp, function, source

SEARCH = 'Telegram/SourceFiles/luxury/ui/utils/itunes_search.cpp'


def check(root, read=source):
    pick = function(read(SEARCH), 'QString pickArtworkUrl(')
    cpp(root, 'itunes_pick', r'''
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
namespace Qt { enum CaseSensitivity { CaseInsensitive }; }
struct QString : std::string {
	using std::string::string;
	QString() = default;
	int compare(const QString &o, Qt::CaseSensitivity) const { return std::string::compare(o); }
};
struct QStringList : std::vector<QString> {
	using std::vector<QString>::vector;
	bool isEmpty() const { return empty(); }
};
template <class T> struct QList : std::vector<T> { using std::vector<T>::vector; };
struct ItunesTrack { QString trackName, artistName, artworkUrl100; };
bool hasCommonArtist(const QStringList &base, const QString &artist) {
	for (const auto &b : base) if (b == artist) return true;
	return false;
}
''' + pick + r'''
int main() {
	const auto tracks = QList<ItunesTrack>{
		{ "other", "Stranger", "stranger.jpg" },
		{ "song (Remastered)", "Band", "edition.jpg" },
		{ "song", "Band", "exact.jpg" },
	};
	assert(pickArtworkUrl(tracks, "song", { "Band" }) == "exact.jpg");
	assert(pickArtworkUrl(tracks, "missing", { "Band" }) == "edition.jpg");
	assert(pickArtworkUrl(tracks, "missing", { "Nobody" }).empty());
	assert(pickArtworkUrl(tracks, "other", {}) == "stranger.jpg");
	assert(pickArtworkUrl(tracks, "missing", {}).empty());
	assert(pickArtworkUrl({}, "song", { "Band" }).empty());
	std::cout << "iTunes cover pick PASS: exact match, then the same artist, never a stranger's cover\n";
}
''')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-itunes-pick-') as directory:
        check(Path(directory))
