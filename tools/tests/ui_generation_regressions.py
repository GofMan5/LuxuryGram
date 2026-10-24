"""Exercise actual theme selector, plugin sticker wait and current-render export with real bundled rpl.
python tools/tests/ui_generation_regressions.py [--cxx g++]
Theme parse/network, QImage, boxes/buttons and preview animation are doubles.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
def read(name): return (root / 'Telegram/SourceFiles' / name).read_text('utf-8')
selector = read('luxury/ui/boxes/theme_selector_box.cpp')
subscriptions = selector[selector.index('\tLuxuryFeatures::MessageShot::themeChosen('):selector.index('\n\tsetDimensionsToContent(')]
header = read('luxury/ui/boxes/theme_selector_box.h')
selection = header[header.index('struct ShotThemeSelection {'):header.index('\nclass ThemeSelectorBox')]
shot = read('luxury/ui/boxes/message_shot_box.cpp')
preview = shot[shot.index('\tconst auto updatePreview = [=]'):shot.index('\n\tif (savedThemeApplyResult ==', shot.index('\tconst auto updatePreview = [=]'))]
assert shot.count('const auto image = _exportImage;') == 2
helper = read('luxury/ui/utils/box_buttons.cpp')
assert 'button->setDisabled(!enabled);' in helper and 'Luxury::Ui::SetBoxButtonEnabled(_applyButton.data(), enabled);' in selector
assert 'Luxury::Ui::SetBoxButtonEnabled(button.data(), enabled);' in shot
assert shot.index('_saveButton = addButton(') < shot.index('_copyButton = addButton(') < shot.index('\n\tupdatePreview();')
assert '_savedThemeLifetime.destroy();\n\t\t\t\t\t_selectedPalette->reset();' in shot
assert 'const auto &cloud = selection.cloud;' in shot and 'const auto embedded = selection.embedded;' in shot
assert 'getSelectedFromCustom()' not in shot and 'getSelectedFromDefault()' not in shot
state = read('luxury/features/message_shot/message_shot_theme_state.cpp')
assert 'rpl::lifetime &lifetime' in state and ') | rpl::on_next(tryFind, lifetime);' in state
cover = read('luxury/ui/boxes/expanded_player_box.cpp')
cover_start = cover.index('void ExpandedPlayerBox::refreshCover(')
cover_reset = cover[cover_start:cover.index('\n\t_mediaView = ', cover_start)]
assert 'setCover(QImage());' in cover_reset
assert cover.index('setCover(QImage());', cover_start) < cover.index('if (document->isSongWithCover())', cover_start)
cover_guard = cover[cover.index('\t\t\tif (!weak || _coverGeneration != generation'):cover.index('\n\t\t});', cover_start)]
updates = read('luxury/utils/telegram_helpers.cpp')
updates = updates[updates.index('rpl::producer<> DocumentLoadUpdates('):]
updates = updates[:updates.index('\n}\n') + 3]
plugin = read('luxury/ui/boxes/plugin_info_box.cpp').replace('\r\n', '\n')
plugin = plugin[plugin.index('\t\tmedia->automaticLoad('):plugin.index('}, session->lifetime());') + len('}, session->lifetime());')]
cpp = r'''
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
template <typename... Args> constexpr bool false_t(Args&&...) { return false; }
#include <gsl/gsl>
using gsl::not_null;
#include <rpl/rpl.h>
namespace base::assertion { void log(const char*, const char*, int) { std::abort(); } }
using QString = std::string;
struct QColor { int value = 0; };
struct QImage { int value = 0; bool isNull() const { return value == 0; } };
using QByteArray = std::string;
namespace style { struct palette { int value = 0; palette() = default; explicit palette(int v) : value(v) {} palette(const palette&) = delete; palette &operator=(const palette &other) { value = other.value; return *this; } QByteArray save() const { return std::to_string(value); } }; }
namespace Data { struct CloudTheme { int id = 0, accessHash = 0, documentId = 0; QString title; }; struct FileOriginTheme { FileOriginTheme(int,int) {} }; }
struct DocumentView { bool ready = false; int value = 0; bool loaded() const { return ready; } int bytes() const { return value; } };
struct Document {
 std::shared_ptr<DocumentView> view = std::make_shared<DocumentView>();
 bool loadingFlag = false; bool loading() const { return loadingFlag; } int stickerSetOrigin() const { return 0; }
 auto createMediaView() { return view; } void save(Data::FileOriginTheme, QString) { loadingFlag = true; }
 struct Location { QString name() const { return {}; } }; Location location() const { return {}; }
};
namespace Window::Theme {
 enum class EmbeddedType { Default = 0, Dark = 1 };
 struct Preview { struct Instance { style::palette palette; } instance; };
 std::unique_ptr<Preview> PreviewFromFile(int bytes, QString, Data::CloudTheme) { if(bytes <= 0) return {}; auto result = std::make_unique<Preview>(); result->instance.palette.value = bytes; return result; }
}
namespace tr { constexpr int now = 0; QString luxury_MessageShotThemePreviewFailed(int) { return "failed"; } QString luxury_MessageShotThemeDefault(int) { return "default"; } QString luxury_MessageShotThemeApply() { return "apply"; } }
namespace Ui {
 struct RoundButton { bool disabled = false; std::function<void()> callback; void setDisabled(bool b) { disabled = b; } void click() { if(!disabled && callback) callback(); } };
 struct Toast { static inline int errors = 0; static void Show(QString) { ++errors; } };
}
namespace LuxuryFeatures::MessageShot {
 rpl::event_stream<Data::CloudTheme> clouds; rpl::event_stream<style::palette> palettes;
 Window::Theme::EmbeddedType embedded = Window::Theme::EmbeddedType(-1); std::optional<QColor> color;
 auto themeChosen() { return clouds.events(); } auto paletteChosen() { return palettes.events(); }
 auto getSelectedFromDefault() { return embedded; } auto getSelectedColorFromDefault() { return color; }
 QString embeddedThemeDisplayName(Window::Theme::EmbeddedType type) { return std::to_string(int(type)); }
 struct Config {};
 std::vector<std::function<void(const QImage&, bool)>> renders;
 template<class Box, class Callback> void Make(Box*, const Config&, Callback callback) { renders.push_back(std::move(callback)); }
}
struct DataOwner { std::map<int, Document> docs; rpl::event_stream<not_null<Document*>> progress; Document *document(int id) { return &docs[id]; } auto documentLoadProgress() { return progress.events(); } };
struct Session { DataOwner owner; rpl::event_stream<> downloads; rpl::lifetime life; auto &data() { return owner; } auto downloaderTaskFinished() { return downloads.events(); } auto &lifetime() { return life; } };
struct Controller { Session owner; auto &session() { return owner; } };
namespace Main { using Session = ::Session; }
using DocumentData = Document;
UPDATES_HERE
struct PluginMetadata { int id = 0; };
struct StickerMedia { Document *doc; void automaticLoad(int, std::nullptr_t) { doc->loadingFlag = true; } bool loaded() const { return doc->view->ready; } };
void waitPluginSticker(Session *session, Document *doc, std::shared_ptr<StickerMedia> media, std::shared_ptr<PluginMetadata> shared, std::function<void(PluginMetadata, Document*)> showBox) {
PLUGIN_HERE
}
SELECTION_HERE
struct ThemeSelectorBox {
 Controller *_controller;
 rpl::event_stream<ShotThemeSelection> _themes;
 std::optional<ShotThemeSelection> _selection;
 Ui::RoundButton button; Ui::RoundButton *_applyButton = nullptr;
 rpl::lifetime _selectionLifetime, life;
 rpl::event_stream<> closing;
 int _selectionGeneration = 0;
 explicit ThemeSelectorBox(Controller *c) : _controller(c) {}
 auto &lifetime() { return life; } auto boxClosing() { return closing.events(); } void closeBox() { closing.fire({}); }
 void showToast(QString text) { Ui::Toast::Show(std::move(text)); }
 template<class Callback> auto addButton(QString, Callback callback) { button.callback = callback; return &button; }
 void setApplyEnabled(bool enabled) { if (_applyButton) _applyButton->setDisabled(!enabled); } // Luxury::Ui::SetBoxButtonEnabled double.
 void setup() {
SUBSCRIPTIONS_HERE
 }
};
struct ImageView { QImage image; std::vector<QImage> deferred; QImage getImage() const { return image; } void setImage(const QImage &v) { if(image.isNull()) image = v; else deferred.push_back(v); } };
struct ShotBox {
 LuxuryFeatures::MessageShot::Config _config;
 QImage _exportImage; Ui::RoundButton save, copy; Ui::RoundButton *_saveButton = &save, *_copyButton = &copy;
 int generationValue = 0; bool alive = true; ImageView view;
 void setExportButtonsEnabled(bool enabled) { for (auto button : { _saveButton, _copyButton }) button->setDisabled(!enabled); }
 auto prepare() {
  auto generation = &generationValue; auto weak = &alive; auto imageView = &view;
PREVIEW_HERE
  return updatePreview;
 }
};
struct Cover { QImage value; QImage image() const { return value; } };
struct CoverBox {
 int _coverGeneration = 0; Cover cover; Cover *_cover = &cover; Ui::RoundButton save;
 void setCover(QImage image) { cover.value = image; save.setDisabled(image.isNull()); }
 void newTrack() { ++_coverGeneration; setCover(QImage()); }
 void arrived(int generation, QImage image) {
  const auto weak = true;
COVER_GUARD_HERE
 }
};
int main() {
 Controller controller; ThemeSelectorBox box(&controller); int applied = 0; ShotThemeSelection committed;
 box._themes.events() | rpl::on_next([&](const ShotThemeSelection &s) { ++applied; committed = s; }, box.life); box.setup();
 assert(box.button.disabled); box.button.click(); assert(!applied);
 auto cloud = [&](int id) { LuxuryFeatures::MessageShot::clouds.fire_copy({id, 0, id, "cloud" + std::to_string(id)}); };
 auto loaded = [&](int id, int palette) { auto &view = *controller.owner.owner.docs[id].view; view.ready = true; view.value = palette; controller.owner.downloads.fire({}); };
 cloud(1); assert(box.button.disabled); loaded(1, 11); assert(!box.button.disabled && box._selection->palette == "11" && box._selection->cloud->id == 1);
 cloud(2); assert(box.button.disabled && !box._selection); box.button.click(); assert(!applied);
 LuxuryFeatures::MessageShot::embedded = Window::Theme::EmbeddedType::Dark; LuxuryFeatures::MessageShot::color = QColor{77}; LuxuryFeatures::MessageShot::palettes.fire_copy(style::palette(22));
 loaded(2, 33); assert(box._selection->palette == "22" && !box._selection->cloud && box._selection->color->value == 77);
 box.button.click(); assert(applied == 1 && committed.palette == "22" && committed.embedded == Window::Theme::EmbeddedType::Dark && !committed.cloud);
 // Fresh selector: two pending choices, failed newest parse, then successful cloud.
 ThemeSelectorBox second(&controller); second.setup(); cloud(3); cloud(4); loaded(3, 30); assert(!second._selection && second.button.disabled); loaded(4, -1); assert(!second._selection && second.button.disabled && Ui::Toast::errors > 0);
 cloud(5); loaded(5, 50); assert(second._selection->cloud->id == 5 && second._selection->palette == "50"); second.closeBox(); cloud(6); second.closeBox(); loaded(6, 60); assert(!second._selection && second.button.disabled);
 ShotBox shot; auto refresh = shot.prepare(); refresh(); auto old = LuxuryFeatures::MessageShot::renders.back(); old({1}, true); assert(shot._exportImage.value == 1 && !shot.save.disabled);
 refresh(); auto latest = LuxuryFeatures::MessageShot::renders.back(); assert(shot._exportImage.isNull() && shot.save.disabled && shot.copy.disabled);
 old({9}, true); assert(shot._exportImage.isNull()); latest({2}, false); assert(shot._exportImage.isNull() && shot.save.disabled);
 latest({3}, true); assert(shot._exportImage.value == 3 && !shot.save.disabled && shot.view.getImage().value == 1); // animation still showing old pixels.
 refresh(); LuxuryFeatures::MessageShot::renders.back()({}, true); assert(shot._exportImage.isNull() && shot.save.disabled && shot.copy.disabled);
 // A theme file that fails to download reports the failure and keeps Apply off instead of waiting for good.
 ThemeSelectorBox third(&controller); third.setup(); cloud(7); const auto errors = Ui::Toast::errors; auto failed = [&](int id) { auto &doc = controller.owner.owner.docs[id]; doc.loadingFlag = false; controller.owner.owner.progress.fire(&doc); };
 controller.owner.owner.progress.fire(controller.owner.owner.document(8)); assert(Ui::Toast::errors == errors); failed(7); assert(Ui::Toast::errors > errors && !third._selection && third.button.disabled);
 // The plugin box opens without its sticker when the sticker download fails, and with it when it loads.
 int shown = 0; Document *shownDoc = nullptr; auto show = [&](PluginMetadata, Document *doc) { ++shown; shownDoc = doc; };
 auto sticker = controller.owner.owner.document(20); waitPluginSticker(&controller.owner, sticker, std::make_shared<StickerMedia>(StickerMedia{ sticker }), std::make_shared<PluginMetadata>(), show);
 assert(!shown); controller.owner.owner.progress.fire(controller.owner.owner.document(21)); assert(!shown); failed(20); assert(shown == 1 && !shownDoc);
 auto loadedSticker = controller.owner.owner.document(22); waitPluginSticker(&controller.owner, loadedSticker, std::make_shared<StickerMedia>(StickerMedia{ loadedSticker }), std::make_shared<PluginMetadata>(), show);
 assert(shown == 1); loaded(22, 1); assert(shown == 2 && shownDoc == loadedSticker);
 CoverBox cover; cover.setCover({1}); cover.newTrack(); assert(cover.cover.image().isNull() && cover.save.disabled); cover.arrived(0, {2}); assert(cover.cover.image().isNull()); cover.arrived(1, {3}); assert(cover.cover.image().value == 3 && !cover.save.disabled); cover.newTrack(); cover.arrived(2, {}); assert(cover.cover.image().isNull() && cover.save.disabled);
 std::cout << "PASS: actual theme selection/rpl, theme and plugin sticker download failures, render export generations; cover reset/late guard; Qt/parser/network/animation doubles\n";
}
'''.replace('SELECTION_HERE', selection).replace('SUBSCRIPTIONS_HERE', subscriptions).replace('PREVIEW_HERE', preview).replace('COVER_GUARD_HERE', cover_guard).replace('UPDATES_HERE', updates).replace('PLUGIN_HERE', plugin)
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='luxury-ui-generations-') as directory:
    work = Path(directory); test = work / 'ui.cpp'; test.write_text(cpp, encoding='utf-8')
    exe = work / ('ui.exe' if os.name == 'nt' else 'ui')
    subprocess.run([args.cxx, '-std=c++20', '-O0', '-D_DEBUG', '-I', str(root / 'Telegram/lib_rpl'), '-I', str(root / 'Telegram/lib_base'), '-I', str(root / 'Telegram/ThirdParty/GSL/include'), str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
