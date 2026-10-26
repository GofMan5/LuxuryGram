"""Fixes ported from AyuGram's issue tracker and fix commits stay in place.

Bounded source check of each ported root cause; not a native acceptance.
"""
from pathlib import Path
import re
import tempfile
from library_regressions import ROOT, cpp, function, source


SUGGESTIONS = 'Telegram/SourceFiles/dialogs/ui/dialogs_suggestions.cpp'
CLICK = 'Telegram/SourceFiles/core/click_handler_types.cpp'
INTEGRATION = 'Telegram/SourceFiles/core/ui_integration.cpp'
URLS = 'Telegram/SourceFiles/luxury/luxury_url_handlers.cpp'
PLUGIN = 'Telegram/SourceFiles/luxury/ui/boxes/plugin_info_box.cpp'
COMPOSE = (
    'Telegram/SourceFiles/history/view/controls/'
    'history_view_compose_controls.cpp')
SELECTOR = 'Telegram/SourceFiles/chat_helpers/tabbed_selector.cpp'
STICKERS = 'Telegram/SourceFiles/chat_helpers/stickers_list_widget.cpp'
STICKERS_H = 'Telegram/SourceFiles/chat_helpers/stickers_list_widget.h'
GIFS = 'Telegram/SourceFiles/chat_helpers/gifs_list_widget.cpp'
GIF_VIEW = 'Telegram/SourceFiles/history/view/media/history_view_gif.cpp'
DOCUMENT = 'Telegram/SourceFiles/history/view/media/history_view_document.cpp'
DOCUMENT_H = 'Telegram/SourceFiles/history/view/media/history_view_document.h'
ATTACH = 'Telegram/SourceFiles/inline_bots/bot_attach_web_view.cpp'
PANEL = 'Telegram/SourceFiles/ui/chat/attach/attach_bot_webview.cpp'
WELCOME = (
    'Telegram/SourceFiles/history/view/'
    'history_view_welcome_messages_section.cpp')
SENDING = 'Telegram/SourceFiles/api/api_sending.cpp'
APIWRAP = 'Telegram/SourceFiles/apiwrap.cpp'
ITEM = 'Telegram/SourceFiles/history/history_item.cpp'
OVERLAY = 'Telegram/SourceFiles/media/view/media_view_overlay_widget.cpp'
SYNC = 'Telegram/SourceFiles/luxury/features/forward/luxury_sync.cpp'
HISTORY = 'Telegram/SourceFiles/history/history_widget.cpp'
FORWARD_RICH = (
    'Telegram/SourceFiles/luxury/features/forward/luxury_forward_rich.cpp')
THREAD_HISTORY = 'Telegram/SourceFiles/history/history.cpp'
THREAD_HISTORY_H = 'Telegram/SourceFiles/history/history.h'
REPLIES = 'Telegram/SourceFiles/data/data_replies_list.cpp'
SUBLIST = 'Telegram/SourceFiles/data/data_saved_sublist.cpp'
SHARE = 'Telegram/SourceFiles/boxes/share_box.cpp'
POLLS = 'Telegram/SourceFiles/api/api_polls.cpp'
TODO = 'Telegram/SourceFiles/api/api_todo_lists.cpp'
PEER_TYPE = 'Telegram/SourceFiles/boxes/peers/edit_peer_type_box.cpp'
PEER_MENU = 'Telegram/SourceFiles/window/window_peer_menu.cpp'
TRANSLATE_TRACKER = 'Telegram/SourceFiles/history/view/history_view_translate_tracker.cpp'

HIDDEN_JOIN_REQUESTS = r'''
#include <cstdio>

enum class Privacy { HasUsername, NoUsername };

struct Toggle {
    bool on = false;
    bool toggled() const { return on; }
};

struct Controller {
    struct { Toggle *requestToJoin = nullptr; } _controls;
    bool _isGroup = false;
    bool _originalRequestToJoin = false;
    Privacy privacy = Privacy::NoUsername;
    Privacy getPrivacy() const { return privacy; }
    bool requestsSectionShown(Privacy value) const SHOWN
    bool requestToJoin() const GETTER
};

int fail(const char *what) {
    std::printf("RED: %s\n", what);
    return 1;
}

int main() {
    auto toggle = Toggle{ .on = true };
    auto channel = Controller();
    channel._controls.requestToJoin = &toggle;
    channel._originalRequestToJoin = true;
    channel.privacy = Privacy::HasUsername;
    if (!channel.requestToJoin()) {
        return fail("saving an untouched public channel turns off join "
            "requests it cannot show and asks about invite links");
    }
    channel._originalRequestToJoin = false;
    toggle.on = false;
    if (channel.requestToJoin()) {
        return fail("a public channel turns join requests on by itself");
    }
    channel.privacy = Privacy::NoUsername;
    channel._originalRequestToJoin = true;
    if (channel.requestToJoin()) {
        return fail("turning join requests off in a private channel is lost");
    }
    toggle.on = true;
    channel._originalRequestToJoin = false;
    if (!channel.requestToJoin()) {
        return fail("turning join requests on in a private channel is lost");
    }
    auto group = Controller();
    group._isGroup = true;
    group._controls.requestToJoin = &toggle;
    group.privacy = Privacy::HasUsername;
    toggle.on = false;
    group._originalRequestToJoin = true;
    if (group.requestToJoin()) {
        return fail("turning join requests off in a public group is lost");
    }
    if (Controller().requestToJoin()) {
        return fail("join requests are on without their toggle");
    }
    std::printf("hidden join requests PASS\n");
    return 0;
}
'''

DELETED_IN_THREAD = r'''
#include <algorithm>
#include <compare>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <vector>
namespace ranges { using std::ranges::lower_bound; }
using MsgId = long long;
constexpr auto ServerMaxMsgId = MsgId(1LL << 56);
struct FullMsgId {
 long long peer = 0;
 MsgId msg = 0;
 friend auto operator<=>(FullMsgId, FullMsgId) = default;
};
struct HistoryItem {
 MsgId id = 0;
 bool thread = true;
 FullMsgId fullId() const { return { 1, id }; }
};
struct Slice {
 std::vector<FullMsgId> ids;
 std::optional<int> skippedBefore;
 std::optional<int> skippedAfter;
};
std::vector<HistoryItem*> _deletedMessages;
void insert(Slice &slice, std::function<bool(HistoryItem*)> belongs)
BODY
std::vector<MsgId> run(
  std::vector<MsgId> ids,
  std::optional<int> before,
  std::optional<int> after) {
 auto slice = Slice{ .skippedBefore = before, .skippedAfter = after };
 for (const auto id : ids) {
  slice.ids.push_back({ 1, id });
 }
 insert(slice, [](HistoryItem *item) { return item->thread; });
 auto result = std::vector<MsgId>();
 for (const auto &id : slice.ids) {
  result.push_back(id.msg);
 }
 return result;
}
int main() {
 auto items = std::vector<HistoryItem>{
  { 12 }, { 9 }, { 7, false }, { 8 }, { 3 } };
 for (auto &item : items) {
  _deletedMessages.push_back(&item);
 }
 const auto expect = [](std::vector<MsgId> got, std::vector<MsgId> want) {
  if (got != want) {
   std::printf("RED: got");
   for (const auto id : got) std::printf(" %lld", id);
   std::printf("\n");
   std::exit(1);
  }
 };
 expect(run({ 10, 8, 5 }, 3, 2), { 10, 9, 8, 5 });
 expect(run({ 10, 8, 5 }, 0, 0), { 12, 10, 9, 8, 5, 3 });
 expect(run({ 10, 8, 5 }, std::nullopt, std::nullopt), { 10, 9, 8, 5 });
 expect(run({}, 0, 0), { 12, 9, 8, 3 });
 expect(run({}, std::nullopt, 0), {});
 std::printf("deleted thread merge PASS\n");
}
'''


def require(condition, message):
    if not condition:
        raise AssertionError('RED: ' + message)


def similar_channels():
    text = source(SUGGESTIONS)
    for name in ('load', 'fill'):
        body = function(text, 'void RecommendationsController::' + name + '(')
        require('hideSimilarChannels()' in body,
            'search still lists similar channels with "Hide similar '
            'channels" on (RecommendationsController::' + name + ')')


def links():
    hidden = function(source(CLICK), 'void HiddenUrlClickHandler::Open(')
    require('IsWebUrl(parsedUrl)' in hidden,
        '"Disable link warning" also skips the box for a hidden file:, '
        'smb: or search-ms: link')
    urls = source(URLS)
    require('bool IsUnsafeExternalUrl(' in urls,
        'nothing tells a file:, smb: or search-ms: link from a web link')
    unsafe = function(urls, 'bool IsUnsafeExternalUrl(')
    for scheme in ('file', 'smb', 'search-ms', 'javascript', 'shell'):
        require('u"' + scheme + '"_q' in unsafe,
            'the shell still opens ' + scheme + ': links')
    require('scheme.size() == 1' in unsafe and 'u"ms-"_q' in unsafe,
        'drive-letter and ms-* links still reach the shell')
    click = function(source(INTEGRATION), 'bool UiIntegration::handleUrlClick(')
    guard = click.find('IsUnsafeExternalUrl(url)')
    require(guard != -1, 'handleUrlClick hands unsafe schemes to the shell')
    require(guard < click.index('File::OpenUrlWithOwnAutoLogin('),
        'the unsafe-scheme check runs after the url is opened')
    for internal in ('u"tg://"_q', 'u"internal:"_q', 'u"tonsite://"_q'):
        require(click.index(internal) < guard,
            internal + ' links are blocked as unsafe before the app routes them')
    plugin = function(source(PLUGIN), 'TextWithEntities ParsePluginDescriptionLinks(')
    require('IsWebUrl(parsed)' in plugin and 'u"tg"_q' in plugin,
        'a plugin description can carry a file: or smb: link')


def compose_ttl():
    body = function(source(COMPOSE), 'void ComposeControls::fieldChanged(')
    ttl = body[body.index('(ttlVisible !='):]
    ttl = ttl[:ttl.index('if (refreshControls)')]
    require('showAutoDeleteButtonInMessageField()' in ttl,
        'with the auto-delete button hidden, every keystroke re-lays out '
        'the compose controls')


def picker_confirmation():
    selector = source(SELECTOR)
    require(selector.count('.requireConfirmation = (_mode == Mode::Full)') == 2,
        'sticker and GIF confirmation also pops up in the media editor and '
        'other non-chat pickers')
    require('bool requireConfirmation = false;' in source(STICKERS_H),
        'a sticker picker asks for confirmation unless told otherwise')
    release = function(source(STICKERS), 'void StickersListWidget::mouseReleaseEvent(')
    require('Mode::ChatIntro' not in release,
        'the chat intro sticker picker asks to confirm a choice it does not send')
    gifs = function(source(GIFS), 'void GifsListWidget::selectInlineResult(')
    require('_requireConfirmation' in gifs,
        'GIF confirmation ignores the picker it was opened from')


def ttl_media():
    paint = function(source(GIF_VIEW), 'CreateTtlPaintCallback(')
    require('icon.width() / 3' not in paint and 'icon.width() * 0.75' in paint,
        'the round-video timer icon is drawn at a third of its size')
    require('std::weak_ptr<rpl::lifetime> _ttlLifetime;' in source(DOCUMENT_H),
        'Document has no handle on its voice timer animation')
    document = source(DOCUMENT)
    require('_ttlLifetime = lifetime;' in document,
        'the voice timer animation is not tracked by its view')
    destructor = function(document, 'Document::~Document()')
    require('_ttlLifetime.lock()' in destructor,
        'the voice timer animation keeps repainting a destroyed view')


def webview():
    join = function(source(ATTACH), 'void WebViewInstance::requestChatJoin(')
    require('MTP_string("tdesktop")' not in join
        and 'WebviewPlatform()' in join,
        'the join-request mini app ignores the spoofed webview platform')
    panel = source(PANEL)
    require('[[nodiscard]] QSize PanelInnerSize()' in panel,
        'the mini app size settings live only in the embedded panel')
    require('LinuxShell::WindowSize(PanelInnerSize())' in panel,
        'the external Linux mini app window ignores the size settings')
    # A mini app opened from the attach menu sends its message on the user's
    # behalf with the flags of these requests.
    attach = source(ATTACH)
    for signature in ('void WebViewInstance::requestButton(',
            'void WebViewInstance::started('):
        body = function(attach, signature)
        require('options.silent ?' not in body
            and 'ShouldSendSilent(' in body,
            'a mini app message ignores "Send without Sound"')


def sending():
    welcome = source(WELCOME)
    require('messageShotSelectionRequest(' in welcome,
        'Message Shot in the welcome messages section does nothing')
    music = function(source(SENDING), 'void SendMusicSelection(')
    require('prependPseudoReply(message)' in music,
        'music sent as a reply to a deleted message fails instead of quoting it')
    require('applyGhostScheduling(' in music,
        'ghost mode sends picked music without the scheduled-send trick')
    batch = function(source(SENDING), 'void SendMusicSelectionBatch(')
    require('reverseLocalPremiumEmoji(itemCaption, history)' in batch,
        'a music caption sends local premium emoji the server rejects')
    rich = function(source(APIWRAP), 'void ApiWrap::sendRichMessage(\n'
        '\t\tstd::shared_ptr<const Iv::RichPage> page,')
    ghost = rich.find('applyGhostScheduling(')
    require(ghost != -1, 'ghost mode sends rich messages without scheduling')
    require(ghost < rich.index('const auto ephemeral ='),
        'rich message ghost scheduling runs after the ephemeral decision')


def ghost_send_paths():
    for path, signature, before, guard, surface in (
            (APIWRAP, 'void ApiWrap::sendInlineResult(', 'sendAction(action)',
                None, 'an inline bot result'),
            (APIWRAP, 'void ApiWrap::forwardMessages(',
                'const auto genClientSideMessage', 'isEphemeral()',
                'a forward'),
            (SHARE, 'ShareBox::SubmitCallback ShareBox::DefaultForwardCallback(',
                'const auto commonSendFlags', 'isEphemeral()',
                'a forward from the share box'),
            (APIWRAP, 'void ApiWrap::sendSharedContact(', 'sendAction(action)',
                'wouldSendMedia(', 'a shared contact'),
            (SENDING, 'void SendSimpleMedia(', 'if (action.options.scheduled)',
                None, 'a location or venue'),
            (POLLS, 'void Polls::create(', 'sendAction(action)', None, 'a poll'),
            (TODO, 'void TodoLists::create(', 'sendAction(action)', None,
                'a checklist')):
        body = function(source(path), signature)
        ghost = body.find('applyGhostScheduling(')
        require(ghost != -1 and ghost < body.index(before),
            f'ghost mode sends {surface} at once instead of scheduling it')
        if guard:
            require(body.rfind(guard, 0, ghost) != -1,
                f'ghost mode schedules {surface} to an ephemeral bot, '
                'which then goes out as an ordinary message')
    simple = function(source(SENDING), 'void SendSimpleMedia(')
    require(simple.index('sendSimpleMedia(') < simple.index('applyGhostScheduling('),
        'ghost mode hides a location reply from its ephemeral bot')
    # The "Send" link in the toast of a tapped dice calls SendDice directly,
    # past the scheduling that sendMessage() gives a dice typed in the field.
    dice = function(source(SENDING), 'bool SendDice(')
    ghost = dice.find('applyGhostScheduling(')
    require(ghost != -1 and ghost < dice.index('api->sendAction(action)'),
        'ghost mode sends a dice from its toast at once instead of scheduling it')
    require(dice.rfind('stakeSeedHash.isEmpty()', 0, ghost) != -1,
        'ghost mode delays a TON stake dice past the game it was staked on')
    require(dice.rfind('wouldSend(message)', 0, ghost) != -1,
        'ghost mode schedules a dice to an ephemeral bot, '
        'which then goes out as an ordinary message')


def hidden_translate_bar():
    setup = function(source(TRANSLATE_TRACKER), 'void TranslateTracker::setup(')
    restore = function(source(PEER_MENU), 'void Filler::addTranslate(')
    require('translateChatEnabledValue()' in setup
        and 'translateChatEnabled()' in restore,
        'the translate bar and its restore item follow different settings')
    bar_needs_premium = 'Premium' in setup.replace('// Upstream also requires Premium', '')
    require(bar_needs_premium or 'premium()' not in restore,
        'without Premium the translate bar is offered but, once hidden, '
        'cannot be brought back from the chat menu')
    require("saveTranslationDisabled(false)" in restore,
        'the chat menu no longer brings a hidden translate bar back')


def hidden_join_requests(root):
    text = source(PEER_TYPE)
    getter = function(text, '[[nodiscard]] bool requestToJoin() const {')
    shown = function(text, 'bool Controller::requestsSectionShown(')
    require('_originalRequestToJoin == data.requestToJoin'
        in function(text, 'void Controller::submit('),
        'saving the channel type no longer skips an unchanged join request')
    cpp(root, 'hidden_join_requests', HIDDEN_JOIN_REQUESTS
        .replace('SHOWN', shown[shown.index('{'):])
        .replace('GETTER', getter[getter.index('{'):]))


def expiring_media():
    expired = function(source(ITEM), 'void HistoryItem::clearMediaAsExpired(')
    require(expired.index('isMessageSavable(this)') < expired.index('auto &owner'),
        'an expired view-once media is cleared although deleted messages are kept')
    badge = function(source(OVERLAY), 'void OverlayWidget::refreshTtlBadge(')
    require('tr::luxury_OnceVideoMessageNote' in badge
        and 'isMessageSavable(_message)' in badge,
        'the viewer still says a kept view-once media opens only once')


def rich_forward():
    send = function(source(SYNC), 'bool sendRichMessageSync(')
    request = send.index('MTPmessages_SendMessage(')
    for step, message in (
            ('applyGhostScheduling(strong, sending.options)',
                'ghost mode sends a forwarded rich message without scheduling'),
            ('StripUnsendableReply(strong, sending.replyTo)',
                'a forwarded rich reply to a deleted message fails as a whole'),
            ('sending.options.effectId = 0',
                'a forwarded rich message carries the effect of the original send')):
        position = send.find(step)
        require(position != -1 and position < request, message)
    require('MTP_long(starsPaid)' in send and 'f_allow_paid_stars' in send,
        'a forwarded rich message is not paid for in a paid chat')
    require('action.options.' not in send[request:],
        'the rich forward request still reads the unprepared send options')
    prepare = function(source(FORWARD_RICH), 'bool prepareBlock(')
    require('case BlockKind::ButtonRow:' in prepare
        and 'isSerializableButton(button.button)' in prepare,
        'one unsupported button turns a forwarded rich page into plain text')


def attach_slot():
    # AyuGram #447: with the attach button hidden, the replace-media button of
    # an edit was drawn over the start of the message field.
    compose = function(source(COMPOSE), 'void ComposeControls::updateControlsGeometry(')
    require('const auto replaceShown = _replaceMedia && !_replaceMedia->isHidden();'
        in compose and 'const auto attachShown = replaceShown' in compose,
        'a hidden attach button frees the slot the replace-media button still uses')
    require(compose.count('attachWidth') >= 3,
        'the field width and the field position disagree about the attach slot')
    history = source(HISTORY)
    for name in ('void HistoryWidget::moveFieldControls(',
            'void HistoryWidget::updateFieldSize('):
        require('(_replaceMedia || settings.showAttachButtonInMessageField())'
            in function(history, name),
            'the replace-media button of an edit overlaps the field with the '
            'attach button hidden (' + name + ')')


def deleted_in_threads(root):
    # AyuGram #454: a topic reloads its list from the server, which no longer
    # returns kept deleted messages, so they vanished after switching topics.
    history = source(THREAD_HISTORY)
    require('void insertDeletedMessages(' in source(THREAD_HISTORY_H),
        'a thread view has no way to see the deleted messages it keeps')
    require('registerDeletedMessage(this)' in function(source(ITEM),
        'void HistoryItem::setDeleted('),
        'a message marked deleted is not remembered by its history')
    require('_deletedMessages.remove(item)' in function(history,
        'void History::itemRemoved('),
        'a destroyed deleted message stays in the list a thread view merges')
    replies = function(source(REPLIES),
        'void RepliesList::injectRootMessageAndReverse(')
    merge = replies.find('insertDeletedMessages(')
    require(merge != -1 and 'inThread(_rootId)' in replies,
        'deleted messages vanish from a topic after switching topics')
    require(merge < replies.index('injectRootMessage(viewer)'),
        'deleted messages are merged after the root, out of order')
    sublist = function(source(SUBLIST), 'bool SavedSublist::buildFromData(')
    require(sublist.count('insertDeleted()') == 2
        and 'sublistPeerId()' in sublist,
        'deleted messages vanish from a saved messages or monoforum chat')
    body = function(history, 'void History::insertDeletedMessages(')
    cpp(root, 'deleted_in_thread',
        DELETED_IN_THREAD.replace('BODY', body[body.index('{'):]))


def deleted_shared_media():
    # AyuGram #174: a kept deleted message stayed in the shared media index,
    # so the Photos/Links counts of a profile kept counting it until restart.
    item = source(ITEM)
    deleted = function(item, 'void HistoryItem::setDeleted(')
    removal = deleted.find('removeFromSharedMediaIndex();')
    require(removal != -1,
        'a deleted message still counts in the shared media of its chat')
    require(removal < deleted.index('_deleted = true;'),
        'a deleted message is dropped from shared media only after it '
        'stops reporting its media types, so nothing is removed')
    types = function(item,
        'Storage::SharedMediaTypesMask HistoryItem::sharedMediaTypes(')
    guard = types.find('if (_deleted) {')
    require(guard != -1 and guard < types.index('Get<HistoryMessageSavedMediaData>'),
        'a deleted message is counted again when its chat is reloaded '
        'or removed twice when it is destroyed')


def square_userpic_upload():
    # AyuGram #352/#433: an uploaded avatar came out cut to a circle. The
    # editor may bake its crop shape into pixels only for CropMode::Mask,
    # which only the sticker creator asks for; avatars must stay square.
    mask = function(source('Telegram/SourceFiles/editor/photo_editor_common.cpp'),
        'void ApplyShapeMask(')
    require(mask.find('cropMode != EditorData::CropMode::Mask') != -1
        and mask.find('return;') < mask.find('drawEllipse'),
        'the photo editor cuts every round crop into the image itself')
    users = []
    for path in (ROOT / 'Telegram/SourceFiles').rglob('*.cpp'):
        text = path.read_text(encoding='utf-8', errors='replace')
        if 'CropMode::Mask' in text or 'ApplyShapeMask(' in text:
            users.append(path.relative_to(ROOT).as_posix())
    require(sorted(users) == [
            'Telegram/SourceFiles/boxes/sticker_creator_box.cpp',
            'Telegram/SourceFiles/editor/photo_editor_common.cpp',
            'Telegram/SourceFiles/editor/photo_editor_controls.cpp',
        ], 'a new path bakes the crop shape into an image: ' + ', '.join(users))


def taskbar_icon_shortcuts():
    # AyuGram #392: every launch told Explorer to drop its icon cache, so all
    # desktop icons were redrawn. Shortcuts are found by name, which AyuGram
    # and other installs share, so the icon must go only to shortcuts that
    # launch this executable, and Explorer is notified only after a rewrite.
    text = source('Telegram/SourceFiles/luxury/utils/windows_utils.cpp')
    icon = function(text, 'bool processIcon(')
    owner = icon.find('GetUniqueFileId(target) != myid')
    require(owner != -1 and owner < icon.find('SetIconLocation('),
        'a shortcut of another program gets our icon by its name')
    # AyuGram #349: an icon the user set on the shortcut in Explorer was
    # replaced by ours on the next launch that rewrote the .ico in tdata.
    kept = icon.find('!ownsIcon(pShellLink, myid)')
    require(kept != -1 and kept < icon.find('SetIconLocation('),
        'a custom shortcut icon is overwritten by the app icon')
    owns = function(text, 'bool ownsIcon(')
    require('GetIconLocation(' in owns and 'u"tdata"_q' in owns
        and 'u"LuxuryGram-"_q' in owns and 'u"AyuGram"_q' in owns,
        'ownsIcon does not tell our tdata icons from the user\'s')
    save = icon.find('->Save(')
    require(save != -1 and icon.count('return true;') == 1
        and save < icon.find('return false;', save) < icon.find('return true;'),
        'a shortcut reports a rewrite it did not save')
    reload = function(text, 'void reloadAppIconFromTaskBar(')
    notify = reload.find('SHChangeNotify(')
    require(notify != -1
        and reload.rfind('if (pinned || shortcuts || legacy)', 0, notify) != -1,
        'Explorer is told to rebuild every icon on each launch')
    for walker in ('bool processLegacy(', 'bool processNewPinned(',
            'bool processNewShortcuts('):
        body = function(text, walker)
        require('changed |= processIcon(' in body
            and body.rstrip().endswith('return changed;\n}'),
            walker + ' drops whether a shortcut was rewritten')


def adaptive_layout_setting():
    # AyuGram #484: "Adaptive layout for wide screens" was shown only while
    # the open chat was already wide, so in a narrower window neither Chat
    # Settings nor its search result or deep link could reach it.
    text = source('Telegram/SourceFiles/settings/sections/settings_chat.cpp')
    body = function(text, 'void SetupChatBackground(')
    label = body.index('tr::lng_settings_adaptive_wide(tr::now)')
    added = body.rfind('const auto adaptive = inner->add(', 0, label)
    gates = ('adaptive->toggle', 'adaptive->setVisible', 'adaptive->hide',
        '.adaptive()', 'chatLayout')
    require(added != -1 and 'SlideWrap' not in body[added:label]
        and not any(gate in body for gate in gates),
        'the adaptive layout checkbox hides unless the chat is already wide')
    search = function(text, 'void BuildChatBackgroundSection(')
    require('u"chat/adaptive-layout"_q' in search
        and 'u"chat/adaptive-layout"_q, {\n\t\t\tadaptive,' in body,
        'the adaptive layout search result points nowhere')

def tray_counter_logo():
    # AyuGram #310: at 200% the tray counter was a grey corner. The custom
    # logo carries the screen ratio, so Window::Logo().scaledToWidth(16)
    # was a 16-pixel image that painters treated as 8 logical pixels, and
    # the counter drawn over it landed in the bottom-right quarter.
    body = function(
        source('Telegram/SourceFiles/window/main_window.cpp'),
        'QImage Logo() {')
    require('currentAppLogo()' in body
        and 'setDevicePixelRatio(1.)' in body,
        'Window::Logo keeps the screen ratio of the custom logo')

DOWNLOAD_SELECTED = r'''
#include <cassert>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>
struct QString : std::string {
    QString() = default;
    QString(const char *s) : std::string(s) {}
    QString(std::string s) : std::string(std::move(s)) {}
    bool isEmpty() const { return empty(); }
};
template <class T> using Fn = std::function<T>;
template <class T> using not_null = T;
namespace Data { using FileOrigin = int; }
struct QDir {};
struct QFileInfo {
    QString path;
    explicit QFileInfo(const QString &p) : path(p) {}
    QDir dir() const { return {}; }
    QString fileName() const { return path; }
};
struct Session {};
std::deque<Fn<void()>> queued;
void *qApp = nullptr;
void InvokeQueued(void*, Fn<void()> f) { queued.push_back(std::move(f)); }
namespace crl { template <class F> F guard(Session*, F f) { return f; } }
namespace base { QString FileNameFromUserString(QString s) { return s; } }
struct DocumentData {
    bool cache = false;
    QString name;
    std::vector<QString> saves;
    Session owner;
    bool isNull() const { return false; }
    bool forbidsFileSave() const { return false; }
    bool saveToCache() const { return cache; }
    bool saveFromData() { return false; }
    QString filepath(bool) const { return {}; }
    QString filename() const { return name; }
    Session &session() { return owner; }
    void save(Data::FileOrigin, const QString &to) { saves.push_back(to); }
};
QString DocumentFileNameForSave(DocumentData*, bool asNew, const QString&, const QDir&) {
    return asNew ? "<save as dialog>" : "Downloads/video.mp4";
}
struct DocumentSaveClickHandler {
    enum class Mode { ToCacheOrFile, ToFile, ToNewFile };
    static void Save(Data::FileOrigin, not_null<DocumentData*>, Mode = Mode::ToCacheOrFile, Fn<void()> = nullptr);
    static void SaveAndTrack(Data::FileOrigin itemId, not_null<DocumentData*> document, Mode mode = Mode::ToCacheOrFile, Fn<void()> started = nullptr) {
        Save(itemId, document, mode, started);
    }
};
SAVE
std::vector<QString> download(DocumentData &document, const QString &folderPath) {
    std::vector<std::pair<not_null<DocumentData*>, Data::FileOrigin>> documents{ { &document, 1 } };
    LAMBDA;
    saveDocuments(folderPath);
    while (!queued.empty()) { auto f = std::move(queued.front()); queued.pop_front(); f(); }
    return document.saves;
}
int main() {
    auto file = DocumentData{ .name = "video.mp4" };
    assert(download(file, "") == std::vector<QString>{ "Downloads/video.mp4" });
    auto round = DocumentData{ .cache = true };
    const auto saved = download(round, "");
    std::printf("cached document: %s\n", saved.empty() ? "nothing" : ('"' + saved[0] + '"').c_str());
    assert(saved == std::vector<QString>{ "Downloads/video.mp4" });
    auto picked = DocumentData{ .cache = true, .name = "clip.mp4" };
    assert(download(picked, "D:/pick/") == std::vector<QString>{ "D:/pick/clip.mp4" });
    auto saveAs = DocumentData{ .cache = true };
    DocumentSaveClickHandler::Save(1, &saveAs, DocumentSaveClickHandler::Mode::ToNewFile);
    while (!queued.empty()) { auto f = std::move(queued.front()); queued.pop_front(); f(); }
    assert(saveAs.saves == std::vector<QString>{ "<save as dialog>" });
    std::puts("Download Selected PASS: a small cached video lands in Downloads like any file; picked folder and Save As unchanged");
}
'''

def download_selected(root):
    # AyuGram #196: "Download Selected" skipped a small video that Telegram
    # keeps in its media cache (round videos, GIF-like clips, voice), while
    # "Save As" saved it. The menu asked for ToCacheOrFile, which for such a
    # document loads it into the cache and writes no file.
    menu = source('Telegram/SourceFiles/menu/menu_item_download_files.cpp')
    lambda_ = function(menu, 'const auto saveDocuments = [=](') + ';'
    save = function(source('Telegram/SourceFiles/data/data_file_click_handler.cpp'),
        'void DocumentSaveClickHandler::Save(')
    cpp(root, 'download_selected',
        DOWNLOAD_SELECTED.replace('SAVE', save).replace('LAMBDA', lambda_))

def chat_wallpaper_toggle():
    # AyuGram #38: "Disable Custom Backgrounds" reads like the global
    # wallpaper switch, so with it on a wallpaper set for one chat looked
    # broken. It only drops the wallpaper a chat carries itself.
    key = 'luxury_DisableCustomBackgrounds'
    en = re.search(r'^"' + key + r'" = "([^"]*)";',
        source('Telegram/Resources/langs/lang.strings'), re.M).group(1)
    ru = re.search(r'\{ "' + key + r'", "([^"]*)" \}',
        source('Telegram/SourceFiles/luxury/luxury_settings.cpp')).group(1)
    require('Chat' in en and 'Wallpaper' in en
        and 'чат' in ru and 'обои' in ru,
        'the custom backgrounds toggle does not say it is about chat wallpapers')
    text = source('Telegram/SourceFiles/luxury/ui/settings/settings_appearance.cpp')
    start = text.index('.id = u"luxury/disableCustomBackgrounds"_q')
    row = text[start:text.index('});', start)]
    require('u"background"_q' in row and 'u"custom"_q' in row,
        'settings search by the old name no longer finds the toggle')
    require('if (disableCustomBackgrounds && resolved.paper && resolved.paper->media) {'
        in source('Telegram/SourceFiles/window/section_widget.cpp'),
        'the toggle hides more than the chat wallpaper')

CANVAS_CLAMP = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
struct QPointF {
    double px = 0., py = 0.;
    QPointF() = default;
    QPointF(double x, double y) : px(x), py(y) {}
    double x() const { return px; }
    double y() const { return py; }
};
struct QSizeF {
    double w = 0., h = 0.;
    double width() const { return w; }
    double height() const { return h; }
};
struct QRectF {
    double l = 0., t = 0., r = 0., b = 0.;
    QRectF() = default;
    QRectF(QPointF a, QPointF z) : l(a.x()), t(a.y()), r(z.x()), b(z.y()) {}
    double x() const { return l; }
    double y() const { return t; }
    double width() const { return r - l; }
    double height() const { return b - t; }
    QSizeF size() const { return { r - l, b - t }; }
};
struct QMarginsF {
    double m = 5.;
    double left() const { return m; }
    double top() const { return m; }
    double right() const { return m; }
    double bottom() const { return m; }
};
const double &checked_clamp(const double &v, const double &lo, const double &hi) {
    if (hi < lo) {
        std::printf("RED: std::clamp with hi < lo aborts a brush stroke\n");
        std::exit(1);
    }
    return std::clamp(v, lo, hi);
}
bool IsValidPoint(const QPointF &p) {
    return std::isfinite(p.x()) && std::isfinite(p.y());
}
struct Scene {
    QRectF rect;
    QRectF sceneRect() const { return rect; }
};
struct ItemCanvas {
    Scene sceneData;
    QRectF _contentRect;
    QMarginsF _brushMargins;
    Scene *scene() { return &sceneData; }
    void computeContentRect(const QPointF &p) BODY
};
int main() {
    auto canvas = ItemCanvas();
    canvas.sceneData.rect = QRectF(QPointF(0., 0.), QPointF(100., 80.));
    canvas._contentRect = QRectF(QPointF(-20., -20.), QPointF(-10., -10.));
    canvas.computeContentRect(QPointF(50., 40.));
    canvas.computeContentRect(QPointF(-30., 200.));
    canvas._contentRect = QRectF(QPointF(90., 70.), QPointF(150., 120.));
    canvas.computeContentRect(QPointF(50., 40.));
    canvas.computeContentRect(QPointF(
        std::numeric_limits<double>::quiet_NaN(), 10.));
    const auto &rect = canvas._contentRect;
    if (!std::isfinite(rect.l) || !std::isfinite(rect.r)) {
        std::printf("RED: a NaN brush point poisons the content rect\n");
        return 1;
    }
    std::printf("canvas clamp PASS\n");
}
'''


def canvas_clamp(root):
    # AyuGram #260: the first brush stroke on a photo aborted in std::clamp
    # (libstdc++ assertions) because the stored content rect could start
    # left of or above the scene, making the lower bound exceed the upper.
    body = function(
        source('Telegram/SourceFiles/editor/scene/scene_item_canvas.cpp'),
        'void ItemCanvas::computeContentRect(')
    body = body[body.index('{'):].replace('std::clamp(', 'checked_clamp(')
    cpp(root, 'canvas_clamp', CANVAS_CLAMP.replace('BODY', body))


CAPTION_CANCEL = r'''
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
template <typename T>
using Fn = std::function<T>;
struct QString : std::string {
    using std::string::string;
    bool isEmpty() const { return empty(); }
};
struct TextWithTags {
    QString text;
    bool empty() const { return text.empty(); }
};
struct Caption {
    bool hidden = false;
    TextWithTags value;
    bool isHidden() const { return hidden; }
    TextWithTags getTextWithTags() const { return value; }
    TextWithTags getTextWithAppliedMarkdown() const { return value; }
};
struct Requests {
    std::vector<Fn<void(const TextWithTags&)>> consumers;
    bool has_consumers() const { return !consumers.empty(); }
    void fire_copy(const TextWithTags &text) const {
        for (const auto &consumer : consumers) {
            consumer(text);
        }
    }
};
struct Box {
    Caption caption;
    Caption *_caption = &caption;
    Requests _textWithTagsRequests;
    bool _confirmed = false;
    bool _textTaken = false;
    Fn<void()> _cancelledCallback;
    Fn<void(const TextWithTags&)> _cancelled2Callback;
    void requestToTakeTextWithTags() REQUEST
    void closing() CLOSING
};
QString Close(bool hidden, const char *caption, bool takes, bool restores) {
    auto field = QString("draft");
    auto box = Box();
    box.caption.hidden = hidden;
    box.caption.value.text = caption;
    field = QString();
    if (takes) {
        box._textWithTagsRequests.consumers.push_back(
            [&](const TextWithTags &text) { field = text.text; });
    }
    if (restores) {
        box._cancelledCallback = [&] { field = "draft"; };
    }
    box.requestToTakeTextWithTags();
    box.closing();
    return field;
}
void Expect(const QString &field, const char *wanted, const char *what) {
    if (field != wanted) {
        std::printf("RED: %s: field has '%s', expected '%s'\n",
            what,
            field.c_str(),
            wanted);
        std::exit(1);
    }
}
int main() {
    Expect(Close(false, "draft", false, true), "draft",
        "story reply or text-as-file cancelled, nobody takes the caption");
    Expect(Close(false, "draft + more", true, true), "draft + more",
        "chat attach cancelled, caption edits returned to the field");
    Expect(Close(true, "", false, true), "draft",
        "cancelled with the caption hidden");
    Expect(Close(false, "", true, true), "draft",
        "cancelled with an empty caption");
    Expect(Close(false, "draft", false, false), "",
        "a box with no restore callback");
    std::printf("caption cancel PASS\n");
}
'''


def caption_cancel_restore(root):
    # AyuGram #68 (tdesktop): cancelling the send-files box marked the caption
    # as taken back even when the caller never listened for it, so the story
    # reply field and pasted-text-as-file lost the text typed before.
    text = source('Telegram/SourceFiles/boxes/send_files_box.cpp')
    request = function(text, 'void SendFilesBox::requestToTakeTextWithTags(')
    prepare = function(text, 'void SendFilesBox::prepare(')
    start = prepare.index('boxClosing() | rpl::on_next([=] ')
    closing = function(prepare[start:], '[=] ')
    cpp(root, 'caption_cancel', CAPTION_CANCEL
        .replace('REQUEST', request[request.index('{'):])
        .replace('CLOSING', closing[closing.index('{'):]))


def details_mime_fallback(root):
    # Message details resolved the file's mime through QMimeDatabase and
    # printed name(): audio/flac, application/vnd.rar, unknown and empty
    # types came back empty, leaving a "MIME Type" row with nothing under it.
    menu = source('Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp')
    start = menu.index('if (!mediaMime.isEmpty()) {')
    row = menu[start:menu.index('));', start)]
    require('Core::MimeTypeForName(mediaMime)' in row,
        'details mime row must still resolve the mime alias')
    subtext = row[row.rindex(',') + 1:].strip()
    require('mediaMime' in subtext and 'isEmpty()' in subtext,
        'details mime row must fall back to the sent mime string when '
        'QMimeDatabase does not know it, got: ' + subtext)
    strings = source('Telegram/Resources/langs/lang.strings')
    require('"luxury_MessageDetailsMimeTypePC" = "MIME Type";' in strings,
        'details mime row label must spell the MIME acronym in capitals')


SURROGATE_CUTS = r'''
#include <cstdio>
#include <cstdlib>
#include <string>
struct QChar {
    char16_t value = 0;
    QChar() = default;
    QChar(char16_t v) : value(v) {}
    QChar(int v) : value(char16_t(v)) {}
    bool isHighSurrogate() const { return value >= 0xD800 && value < 0xDC00; }
    bool isLowSurrogate() const { return value >= 0xDC00 && value < 0xE000; }
};
struct QString {
    std::u16string value;
    QString() = default;
    QString(std::u16string text) : value(std::move(text)) {}
    QString(const char *utf8) {
        for (auto p = reinterpret_cast<const unsigned char*>(utf8); *p;) {
            if (*p < 0x80) {
                value.push_back(*p++);
            } else {
                value.push_back(char16_t(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F)));
                p += 3;
            }
        }
    }
    bool isEmpty() const { return value.empty(); }
    int size() const { return int(value.size()); }
    int length() const { return size(); }
    QChar operator[](int i) const { return value[i]; }
    QChar at(int i) const { return value[i]; }
    QString left(int n) const { return value.substr(0, std::max(0, n)); }
    QString right(int n) const { return value.substr(size() - std::min(n, size())); }
    QString mid(int from) const { return value.substr(from); }
    bool operator==(const QString &other) const { return value == other.value; }
    friend QString operator+(const QString &a, const QString &b) { return a.value + b.value; }
    friend QString operator+(const QString &a, QChar b) { return a.value + b.value; }
    friend QString operator+(const char *a, const QString &b) { return QString(a).value + b.value; }
};
QString operator""_q(const char16_t *text, std::size_t size) {
    return std::u16string(text, size);
}
constexpr auto kMaxToastPatternLength = TOAST;
constexpr auto kMaxMediaNameLength = NAME;
HELPER
TRUNCATED
QString FileNameRow(const QString &mediaName) {
    ROW
    return shortified;
}
bool Broken(const QString &text) {
    for (auto i = 0; i < text.size(); ++i) {
        const auto ch = text[i];
        if (ch.isHighSurrogate()
            && (i + 1 == text.size() || !text[i + 1].isLowSurrogate())) {
            return true;
        }
        if (ch.isLowSurrogate()
            && (i == 0 || !text[i - 1].isHighSurrogate())) {
            return true;
        }
    }
    return false;
}
QString Repeat(char16_t ch, int count) { return std::u16string(count, ch); }
const auto kEmoji = QString(u"\U0001F600"_q);
int failures = 0;
void Expect(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
int main() {
    SHORTIFY
    const auto quote = shortify(Repeat(u'a', 98) + kEmoji + Repeat(u'b', 10), 100);
    Expect(!Broken(quote), "deleted-reply quote cut through an emoji: Qt toUtf8 sends '?'");
    Expect(quote == Repeat(u'a', 98) + QChar(8230), "deleted-reply quote keeps whole text before the emoji");
    Expect(shortify(Repeat(u'c', 100), 100) == Repeat(u'c', 100), "a 100-character quote fits and is not cut");
    Expect(shortify(Repeat(u'c', 101), 100) == Repeat(u'c', 99) + QChar(8230), "a 101-character quote is cut to 100");
    Expect(shortify(QString(), 100).isEmpty(), "empty quote stays empty");

    const auto pattern = TruncatedPattern(Repeat(u'p', 63) + kEmoji + Repeat(u'q', 3));
    Expect(!Broken(pattern), "filter toast pattern cut through an emoji");
    Expect(pattern == Repeat(u'p', 63) + u"\u2026"_q, "filter toast pattern drops the split emoji");
    Expect(TruncatedPattern(Repeat(u'p', 64)) == Repeat(u'p', 64), "64-character pattern is shown whole");

    const auto name = FileNameRow(Repeat(u'a', 5) + kEmoji + Repeat(u'b', 19));
    Expect(!Broken(name), "details file name cut through an emoji");
    Expect(name == u"\u2026"_q + kEmoji + Repeat(u'b', 19), "details file name keeps the whole emoji");
    Expect(FileNameRow(Repeat(u'n', 20)) == Repeat(u'n', 20), "20-character name is shown whole");
    Expect(FileNameRow(Repeat(u'n', 21)) == u"\u2026"_q + Repeat(u'n', 20), "21-character name keeps 20");
    if (failures) {
        return 1;
    }
    std::printf("surrogate cuts OK\n");
    return 0;
}
'''


def details_counts(root):
    # Views and Shares in the message Details menu printed raw digits
    # ("1234567"), while the views tooltip on the same message groups them
    # through lt_count_decimal -> Lang::FormatCountDecimal ("1,234,567").
    menu = source('Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp')
    tooltip = source('Telegram/SourceFiles/history/view/history_view_bottom_info.cpp')
    lang = source('Telegram/SourceFiles/lang/lang_tag.cpp')
    require('lt_count_decimal' in tooltip, 'views tooltip no longer groups digits')
    require('return QLocale().toString(number);' in function(lang, 'QString FormatCountDecimal('), 'FormatCountDecimal changed')
    body = function(menu, 'void AddMessageDetailsAction(')
    for name, count in (('messageViews', 'item->viewsCount()'), ('messageForwards', 'views->forwardsCount')):
        start = body.index('const auto ' + name + ' =') if ('const auto ' + name + ' =') in body else body.index(name + ' =')
        line = body[start:body.index(';', start)]
        require('Lang::FormatCountDecimal(' + count + ')' in line, name + ' is not grouped like the views tooltip')
        require('QString::number' not in line, name + ' still prints raw digits')
    require('QString::number(item->id.bare)' in body, 'message ID must stay a raw identifier')


def details_dc_names(root):
    # Details > Datacenter showed English city names in every language
    # ("DC2, Amsterdam, NL" under the Russian interface) and mixed a state
    # with a three-letter country next to two-letter codes in English.
    helpers = source('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp')
    strings = source('Telegram/Resources/langs/lang.strings')
    overlay = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')
    body = function(helpers, 'QString getDCName(int dc) {')
    for literal in re.findall(r'u?"([^"]*)"', body):
        require(not re.search('[A-Za-z]{2,}', literal.replace('DC%1', '')), 'datacenter text is hardcoded: ' + literal)
    places = {
        'luxury_DCLocationMiami': ((1, 3), 'Miami, USA', 'Майами, США'),
        'luxury_DCLocationAmsterdam': ((2, 4), 'Amsterdam, Netherlands', 'Амстердам, Нидерланды'),
        'luxury_DCLocationSingapore': ((5,), 'Singapore', 'Сингапур'),
    }
    for key, (dcs, english, russian) in places.items():
        require(f'"{key}" = "{english}";' in strings, key + ' missing from lang.strings')
        require(f'{{ "{key}", "{russian}" }}' in overlay, key + ' has no Russian text')
        require(body.count('tr::' + key + '(tr::now)') == 1, key + ' is not used by getDCName')
        branch = body[:body.index('tr::' + key + '(tr::now)')]
        for dc in dcs:
            require(f'case {dc}:' in branch, f'DC{dc} does not map to {key}')
    require('tr::luxury_UnknownDC(tr::now)' in body, 'unknown datacenter lost its text')


def composer_auto_delete_name(root):
    # Settings > Chats > Message Field Elements named the auto-delete timer
    # button "TTL" in both languages, the only abbreviation in a list of
    # plain words, while Telegram calls the same button Auto-Delete.
    strings = source('Telegram/Resources/langs/lang.strings')
    overlay = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')
    chats = source('Telegram/SourceFiles/luxury/ui/settings/settings_chats.cpp')
    require('"luxury_MessageFieldElementTTL" = "Auto-Delete";' in strings,
        'the auto-delete toggle is not named Auto-Delete')
    require('{ "luxury_MessageFieldElementTTL", "Автоудаление" }' in overlay,
        'the auto-delete toggle has no Russian name')
    toggle = chats[chats.index('.id = u"luxury/showAutoDeleteButtonInMessageField"_q'):]
    toggle = toggle[:toggle.index('});')]
    require('.title = tr::luxury_MessageFieldElementTTL()' in toggle
        and '.icon = { &st::messageFieldTTLIcon }' in toggle,
        'the auto-delete toggle lost its title or icon')


def field_and_mark_cuts(root):
    # Pasting text longer than a field's limit cut it at a fixed UTF-16
    # position, so an emoji on the boundary stayed half in the field; the
    # deleted and edited marks were cut the same way on save and on load.
    field = function(
        source('Telegram/lib_ui/ui/widgets/fields/input_field.cpp'),
        'void InputField::chopByMaxLength(')
    settings = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')
    helpers = source('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp')
    setters = (function(settings, 'void LuxurySettings::setDeletedMark(')
        + '\n' + function(settings, 'void LuxurySettings::setEditedMark('))
    start = settings.index('auto validateText = [&]')
    validate = settings[start:settings.index('\n\t};', start) + 4]
    mark = re.search(r'kMaxMarkLength = (\d+);', settings).group(1)
    cpp(root, 'mark_cuts', MARK_CUTS
        .replace('MARK', mark)
        .replace('HELPER', function(helpers, 'int safeCutPosition('))
        .replace('CHOP', field)
        .replace('SETTERS', setters)
        .replace('VALIDATE', validate))


def plugin_and_setting_cuts(root):
    # Plugin names, authors and descriptions, the message-shot cloud theme
    # title and the monospace font family were cut at a fixed UTF-16 index;
    # an emoji on the boundary left a lone surrogate that drew as a box.
    plugin = source('Telegram/SourceFiles/luxury/ui/boxes/plugin_info_box.cpp')
    settings = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')
    helpers = source('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp')
    parse = function(plugin, 'PluginMetadata ParsePluginMetadata(')
    require('.left(' not in parse and parse.count('Truncated(') == 9,
        'plugin metadata is still cut at a fixed UTF-16 index')
    cloud = function(settings, 'void MessageShotSettings::setCloudTheme(')
    start = cloud.index('const auto validatedTitle = ')
    title = cloud[start:cloud.index(';', start) + 1]
    cpp(root, 'plugin_setting_cuts', PLUGIN_SETTING_CUTS
        .replace('FONT_LIMIT', re.search(r'kMaxFontFamilyLength = (\d+);', settings).group(1))
        .replace('TITLE_LIMIT', re.search(r'kMaxThemeTitleLength = (\d+);', settings).group(1))
        .replace('HELPER', function(helpers, 'int safeCutPosition('))
        .replace('TRUNCATED', function(plugin, 'QString Truncated('))
        .replace('MONO', function(settings, 'void LuxurySettings::setMonoFont('))
        .replace('TITLE', title))


PLUGIN_SETTING_CUTS = r'''
#include <algorithm>
#include <cstdio>
#include <string>
struct QChar {
    char16_t value = 0;
    QChar(char16_t v) : value(v) {}
    bool isHighSurrogate() const { return value >= 0xD800 && value < 0xDC00; }
    bool isLowSurrogate() const { return value >= 0xDC00 && value < 0xE000; }
};
struct QString {
    std::u16string value;
    QString() = default;
    QString(std::u16string text) : value(std::move(text)) {}
    int size() const { return int(value.size()); }
    QChar operator[](int i) const { return value[i]; }
    QString left(int n) const { return value.substr(0, std::max(0, n)); }
    bool operator==(const QString &other) const { return value == other.value; }
};
template <typename T>
struct Var {
    T value;
    const T &current() const { return value; }
    Var &operator=(const T &v) { value = v; return *this; }
};
constexpr auto kMaxFontFamilyLength = FONT_LIMIT;
constexpr auto kMaxThemeTitleLength = TITLE_LIMIT;
HELPER
TRUNCATED
struct LuxurySettings {
    Var<QString> _monoFont;
    int saves = 0;
    void save() { ++saves; }
    void setMonoFont(const QString &val);
};
MONO
QString CloudTitle(const QString &title) {
    TITLE
    return validatedTitle;
}
bool Broken(const QString &text) {
    for (auto i = 0; i < text.size(); ++i) {
        if (text[i].isHighSurrogate()
            && (i + 1 == text.size() || !text[i + 1].isLowSurrogate())) {
            return true;
        }
        if (text[i].isLowSurrogate()
            && (i == 0 || !text[i - 1].isHighSurrogate())) {
            return true;
        }
    }
    return false;
}
const auto kEmoji = std::u16string(u"\U0001F9E9");
int failures = 0;
void Expect(bool ok, const char *what, int limit) {
    if (!ok) {
        std::printf("FAIL %s at %d\n", what, limit);
        ++failures;
    }
}
QString Boundary(int limit) {
    return std::u16string(limit - 1, u'a') + kEmoji + u"zz";
}
QString Font(const QString &value) {
    auto settings = LuxurySettings();
    settings.setMonoFont(value);
    return settings._monoFont.current();
}
int main() {
    for (const auto limit : { 32, 64, 100, 128, 200, 8000 }) {
        const auto cut = Truncated(Boundary(limit), limit);
        Expect(!Broken(cut) && cut.size() == limit - 1, "plugin field keeps no half emoji", limit);
        const auto ascii = Truncated(std::u16string(limit + 5, u'x'), limit);
        Expect(ascii.size() == limit, "plugin field keeps its full length", limit);
        const auto whole = Truncated(u"ab" + kEmoji, limit);
        Expect(whole.size() == 4, "short plugin field is untouched", limit);
    }
    const auto font = Font(Boundary(kMaxFontFamilyLength));
    Expect(!Broken(font) && font.size() == kMaxFontFamilyLength - 1, "monospace font keeps no half emoji", kMaxFontFamilyLength);
    Expect(Font(std::u16string(kMaxFontFamilyLength + 3, u'x')).size() == kMaxFontFamilyLength, "monospace font keeps its full length", kMaxFontFamilyLength);
    const auto title = CloudTitle(Boundary(kMaxThemeTitleLength));
    Expect(!Broken(title) && title.size() == kMaxThemeTitleLength - 1, "cloud theme title keeps no half emoji", kMaxThemeTitleLength);
    Expect(CloudTitle(std::u16string(kMaxThemeTitleLength + 3, u'x')).size() == kMaxThemeTitleLength, "cloud theme title keeps its full length", kMaxThemeTitleLength);
    if (failures) {
        return 1;
    }
    std::printf("plugin and setting cuts: OK\n");
    return 0;
}
'''


def surrogate_safe_cuts(root):
    # Deleted-reply quotes, filter toasts and the details file name cut user
    # text at a fixed UTF-16 index. An emoji on the boundary left a lone
    # surrogate: Qt's toUtf8 encodes it as '?', so the quote sent to the
    # chat read "...?..." instead of the emoji, and the menus drew a box.
    helpers = source('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp')
    filters = source('Telegram/SourceFiles/luxury/features/filters/filters_utils.cpp')
    menu = source('Telegram/SourceFiles/luxury/ui/context_menu/context_menu.cpp')
    helper = function(helpers, 'int safeCutPosition(') if 'int safeCutPosition(' in helpers else ''
    start = helpers.index('const auto shortify = [&]')
    shortify = helpers[start:helpers.index('\n\t};', start) + 4]
    truncated = function(filters, 'QString TruncatedPattern(')
    start = menu.index('const auto shortified = ')
    row = menu[start:menu.index(';', start) + 1]
    toast = re.search(r'kMaxToastPatternLength = (\d+);', filters).group(1)
    name = re.search(r'kMaxMediaNameLength = (\d+);', menu).group(1)
    cpp(root, 'surrogate_cuts', SURROGATE_CUTS
        .replace('TOAST', toast)
        .replace('NAME', name)
        .replace('HELPER', helper)
        .replace('TRUNCATED', truncated)
        .replace('ROW', row)
        .replace('SHORTIFY', shortify))


MARK_CUTS = r'''
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#define Expects(x) do { if (!(x)) std::abort(); } while (false)
struct QChar {
    char16_t value = 0;
    QChar() = default;
    QChar(char16_t v) : value(v) {}
    bool isHighSurrogate() const { return value >= 0xD800 && value < 0xDC00; }
    bool isLowSurrogate() const { return value >= 0xDC00 && value < 0xE000; }
};
struct QString {
    std::u16string value;
    QString() = default;
    QString(std::u16string text) : value(std::move(text)) {}
    int size() const { return int(value.size()); }
    QChar operator[](int i) const { return value[i]; }
    QString left(int n) const { return value.substr(0, std::max(0, n)); }
    bool operator==(const QString &other) const { return value == other.value; }
};
struct QTextDocument {
    std::u16string text;
    QChar characterAt(int i) const {
        return (i >= 0 && i < int(text.size())) ? QChar(text[i]) : QChar(char16_t(0x2029));
    }
};
struct QTextCursor {
    enum MoveMode { MoveAnchor, KeepAnchor };
    enum MoveOperation { End };
    QTextDocument *doc = nullptr;
    int pos = 0;
    int anchor = 0;
    explicit QTextCursor(QTextDocument *d) : doc(d) {}
    void movePosition(MoveOperation) { pos = anchor = int(doc->text.size()); }
    int position() const { return pos; }
    void setPosition(int p, MoveMode mode = MoveAnchor) {
        if (p < 0 || p > int(doc->text.size())) return;
        pos = p;
        if (mode == MoveAnchor) anchor = p;
    }
    void removeSelectedText() {
        const auto from = std::min(pos, anchor);
        doc->text.erase(from, std::max(pos, anchor) - from);
        pos = anchor = from;
    }
};
namespace Ui {
struct InputField {
    bool _correcting = true;
    int _maxLength = -1;
    QTextDocument doc;
    QTextDocument *document() { return &doc; }
    void chopByMaxLength(int insertPosition, int insertLength);
};
CHOP
} // namespace Ui
template <typename T>
struct Var {
    T value;
    const T &current() const { return value; }
    Var &operator=(const T &v) { value = v; return *this; }
};
constexpr auto kMaxMarkLength = MARK;
HELPER
namespace tr {
struct now_t {};
inline constexpr now_t now;
struct phrase { QString operator()(now_t) const { return QString(u"edited"); } };
inline constexpr phrase lng_edited;
} // namespace tr
struct LuxurySettings {
    Var<QString> _deletedMark;
    Var<QString> _editedMark;
    int saves = 0;
    void save() { ++saves; }
    void setDeletedMark(const QString &val);
    void setEditedMark(const QString &val);
};
SETTERS
QString Validate(const QString &text, int limit) {
    auto var = Var<QString>{ text };
    auto modified = false;
    VALIDATE
    validateText(var, limit);
    return var.current();
}
bool Broken(const QString &text) {
    for (auto i = 0; i < text.size(); ++i) {
        if (text[i].isHighSurrogate()
            && (i + 1 == text.size() || !text[i + 1].isLowSurrogate())) {
            return true;
        }
        if (text[i].isLowSurrogate()
            && (i == 0 || !text[i - 1].isHighSurrogate())) {
            return true;
        }
    }
    return false;
}
std::u16string Repeat(const std::u16string &part, int count) {
    auto result = std::u16string();
    while (count--) result += part;
    return result;
}
const auto kEmoji = std::u16string(u"\U0001F9F9");
int failures = 0;
void Expect(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
QString Pasted(const std::u16string &text, int limit) {
    auto field = Ui::InputField();
    field._maxLength = limit;
    field.doc.text = text;
    field.chopByMaxLength(0, int(text.size()));
    return QString(field.doc.text);
}
QString Typed(const std::u16string &before, const std::u16string &typed, int limit) {
    auto field = Ui::InputField();
    field._maxLength = limit;
    field.doc.text = before + typed;
    field.chopByMaxLength(int(before.size()), int(typed.size()));
    return QString(field.doc.text);
}
int main() {
    const auto odd = u"a" + Repeat(kEmoji, 40);
    const auto tail = std::u16string(63, u'a') + kEmoji;
    const auto ascii = std::u16string(70, u'x');
    const auto even = Repeat(kEmoji, 40);

    const auto pastedOdd = Pasted(odd, 64);
    Expect(!Broken(pastedOdd) && pastedOdd.size() == 63, "paste a+40 emoji into a 64 field");
    const auto pastedTail = Pasted(tail, 64);
    Expect(!Broken(pastedTail) && pastedTail.size() == 63, "paste 63a+emoji into a 64 field");
    const auto typedTail = Typed(std::u16string(63, u'a'), kEmoji, 64);
    Expect(!Broken(typedTail) && typedTail.size() == 63, "type an emoji at position 63 of a 64 field");
    Expect(Pasted(even, 64).size() == 64 && !Broken(Pasted(even, 64)), "paste 40 emoji keeps 32 whole");
    Expect(Pasted(ascii, 64).size() == 64, "ascii is cut at the limit");
    Expect(Typed(u"ab", u"c", 64).size() == 3, "short text untouched");

    auto settings = LuxurySettings();
    settings.setDeletedMark(QString(odd));
    Expect(!Broken(settings._deletedMark.current()) && settings._deletedMark.current().size() == 63, "deleted mark keeps whole pairs");
    settings.setEditedMark(QString(odd));
    Expect(!Broken(settings._editedMark.current()) && settings._editedMark.current().size() == 63, "edited mark keeps whole pairs");
    settings.setDeletedMark(QString(ascii));
    Expect(settings._deletedMark.current().size() == 64, "ascii mark is cut at the limit");
    const auto loaded = Validate(QString(odd), kMaxMarkLength);
    Expect(!Broken(loaded) && loaded.size() == 63, "loaded mark keeps whole pairs");
    Expect(Validate(QString(ascii), kMaxMarkLength).size() == 64, "loaded ascii mark is cut at the limit");
    if (failures) {
        return 1;
    }
    std::printf("Field and mark cuts PASS: paste/type at the limit and mark setters/loader keep whole surrogate pairs\n");
}
'''


MEDIA_SIZE = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
using int64 = std::int64_t;
using qint64 = std::int64_t;
struct QString {
    std::string value;
    QString() = default;
    QString(const char *text) : value(text) {}
    QString(std::string text) : value(std::move(text)) {}
    static QString number(qint64 number) { return std::to_string(number); }
    QString operator+(const QString &other) const { return value + other.value; }
    QString operator+(char other) const { return value + other; }
};
QString operator""_q(const char16_t *text, std::size_t size) {
    return std::string(text, text + size);
}
namespace Data {
enum class PhotoSize { Large, Small, Thumbnail };
struct Document { int64 size = 0; };
struct Photo {
    bool video = false;
    bool hasVideo() const { return video; }
    int imageByteSize(PhotoSize) const { return 0; }
    int videoByteSize(PhotoSize) const { return 0; }
};
struct Media {
    Document *doc = nullptr;
    Document *document() const { return doc; }
    Photo *photo() const { return nullptr; }
};
} // namespace Data
struct HistoryItem {
    Data::Media *value = nullptr;
    Data::Media *media() const { return value; }
};
template <typename T>
using not_null = T;
namespace Ui {
FORMAT
} // namespace Ui
BYTES
SIZE
int main() {
    const struct { int64 bytes; const char *text; } cases[] = {
        { 1536, "1.5 KB" },
        { 2097152000LL, "2000.0 MB" },
        { 2684354560LL, "2560.0 MB" },
        { 4194304000LL, "4000.0 MB" },
    };
    for (const auto &[bytes, text] : cases) {
        auto document = Data::Document{ bytes };
        auto media = Data::Media{ &document };
        const auto item = HistoryItem{ &media };
        const auto shown = getMediaSize(&item).value;
        if (shown != text) {
            std::printf("RED: %lld bytes shown as \"%s\", expected \"%s\"\n",
                (long long)bytes, shown.c_str(), text);
            std::exit(1);
        }
    }
    std::printf("media size details PASS\n");
}
'''


def media_size_details(root):
    # Message details printed a negative byte count for files over 2 GiB:
    # getMediaSizeBytes returned int while DocumentData::size is int64, and
    # MSVC builds with C4244 disabled, so the narrowing was silent.
    helpers = source('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp')
    formats = source('Telegram/SourceFiles/ui/text/format_values.cpp')
    bytes_start = helpers.index('getMediaSizeBytes(not_null<HistoryItem*> message) {')
    bytes_start = helpers.rindex('\n', 0, bytes_start) + 1
    bytes_body = helpers[bytes_start:helpers.index('\n}\n', bytes_start) + 3]
    size_start = helpers.index('QString getMediaSize(not_null<HistoryItem*> message) {')
    size_body = helpers[size_start:helpers.index('\n}\n', size_start) + 3]
    format_start = formats.index('QString FormatSizeText(qint64 size) {')
    format_body = formats[format_start:formats.index('\n}\n', format_start) + 3]
    cpp(root, 'media_size', MEDIA_SIZE
        .replace('FORMAT', format_body)
        .replace('BYTES', bytes_body.replace('not_null<HistoryItem*>', 'const HistoryItem *'))
        .replace('SIZE', size_body.replace('not_null<HistoryItem*>', 'const HistoryItem *')))


ZALGO_MARKS = r'''
#include <cstdio>
#include <cstdlib>
#include <vector>
using uint = unsigned int;
struct Range { uint from, till; };
const Range kMn[] = { RANGES };
struct QChar {
    enum Category { Mark_NonSpacing, Other };
    static Category category(uint code) {
        for (const auto &range : kMn) {
            if (code >= range.from && code <= range.till) {
                return Mark_NonSpacing;
            }
        }
        return Other;
    }
};
struct QStringView {
    std::vector<uint> codes;
    std::vector<uint> toUcs4() const { return codes; }
};
BODY
int main() {
    const std::vector<std::vector<uint>> keep = { KEEP };
    const std::vector<std::vector<uint>> strip = { STRIP };
    for (const auto &run : keep) {
        if (IsZalgo({ run })) {
            std::printf("RED: script marks starting U+%04X taken for zalgo\n", run[0]);
            std::exit(1);
        }
    }
    for (const auto &run : strip) {
        if (!IsZalgo({ run })) {
            std::printf("RED: zalgo starting U+%04X kept\n", run[0]);
            std::exit(1);
        }
    }
    std::printf("zalgo script marks PASS: %zu kept, %zu stripped\n", keep.size(), strip.size());
}
'''


def zalgo_script_marks(root):
    # AyuGram #390: "Filter Zalgo" replaced every run of three nonspacing
    # marks, so Burmese "\u101c\u102d\u102f\u1037" showed as a bare
    # "\u101c"; Tibetan stacks, pointed Hebrew and Devanagari lost marks too.
    import unicodedata
    text = source('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp')
    filtered = function(text, 'QString filterZalgo(')
    require('if (!IsZalgo(match.capturedView())) {' in filtered
        and filtered.index('IsZalgo(') < filtered.index('QChar(0x2060)'),
        'every run of three marks is still replaced')
    start = text.index('constexpr auto kMaxScriptMarks')
    body = text[start:text.index('\n}\n', text.index('bool IsZalgo(')) + 3]
    ranges, first = [], None
    for code in range(0x110000):
        mark = unicodedata.category(chr(code)) == 'Mn'
        if mark and first is None:
            first = code
        elif not mark and first is not None:
            ranges.append((first, code - 1))
            first = None
    def runs(words):
        return ', '.join('{ ' + ', '.join(hex(ord(c)) for c in w) + ' }'
            for w in words)
    keep = [
        '\u102d\u102f\u1037',
        '\u103e\u102f\u1036\u1037',
        '\u0f92\u0fb2\u0f74',
        '\u0f92\u0fb1\u0f74',
        '\u05b4\u05bc\u05c1',
        '\u05b0\u05bc\u05c1',
        '\u093c\u0947\u0902',
        '\u0302\u0323',
        '\u0654\u064e\u0651',
    ]
    strip = [
        '\u0335\u0321\u0322\u034e',
        '\u0337\u0327\u031b\u0359',
        '\u0e47\u0e47\u0e47\u0e47',
        '\u102d\u102d\u102d',
        '\u064b\u064c\u064d\u064e\u064f\u0650\u0651\u0652',
        '\u102d\u102e\u102f\u1030\u1032\u1036',
        '\u0301\u0302\u0303',
        '\u202e',
        '\u200f',
    ]
    cpp(root, 'zalgo_marks', ZALGO_MARKS
        .replace('RANGES', ', '.join('{ %#x, %#x }' % r for r in ranges))
        .replace('BODY', body)
        .replace('KEEP', runs(keep))
        .replace('STRIP', runs(strip)))


OFFLINE_FOLDER = r'''
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
using crl_time = long long;
namespace crl { using time = crl_time; }
constexpr auto kFolderCheckTimeout = crl::time(2000);
struct QString : std::string {
    QString() = default;
    QString(const char *s) : std::string(s) {}
    bool isEmpty() const { return empty(); }
};
int hangMs = 0;
bool present = true;
struct QDir {
    QString path;
    explicit QDir(const QString &p) : path(p) {}
    bool exists() const {
        std::this_thread::sleep_for(std::chrono::milliseconds(hangMs));
        return present;
    }
};
struct QStandardPaths {
    enum Location { DownloadLocation };
    static QString writableLocation(Location) { return "C:/Users/u/Downloads"; }
};
BODY
int main() {
    using clock = std::chrono::steady_clock;
    const auto share = QString("//offline/share");
    assert(ReachableFolder(share) == share);
    present = false;
    assert(ReachableFolder(share) == "C:/Users/u/Downloads");
    assert(ReachableFolder(QString()) == "C:/Users/u/Downloads");
    present = true;
    hangMs = 30000;
    const auto started = clock::now();
    const auto result = ReachableFolder(share);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now() - started).count();
    std::printf("offline share: %lld ms -> %s\n", waited, result.c_str());
    assert(result == "C:/Users/u/Downloads");
    assert(waited < 5000);
    // The share is still hanging: the next save must not wait again.
    const auto again = clock::now();
    assert(ReachableFolder(share) == "C:/Users/u/Downloads");
    const auto waitedAgain = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now() - again).count();
    std::printf("still offline: %lld ms\n", waitedAgain);
    assert(waitedAgain < 500);
    std::fflush(stdout);
    std::_Exit(0);
}
'''


def offline_save_folder(root):
    # AyuGram #270: with the remembered dialog folder on a network share that
    # went offline, Save As froze the app until it crashed. Each file system
    # call on that path blocks for tens of seconds on the main thread: the
    # default file name probes it, then the dialog opens in it.
    text = source('Telegram/SourceFiles/core/file_utilities.cpp')
    body = function(text, 'QString ReachableFolder(')
    require('std::thread(' in body and '.detach()' in body
        and 'wait_for(' in body and 'QDir(path).exists()' in body,
        'the remembered folder is probed on the main thread')
    cpp(root, 'offline_save_folder', OFFLINE_FOLDER.replace('BODY', body))
    name = function(text, 'QString filedialogDefaultName(')
    reach = name.find('ReachableFolder(cDialogLastPath())')
    require(reach != -1 and reach < name.find('QFileInfo::exists('),
        'the default file name probes an offline folder')
    fallback = function(text, 'bool GetDefault(')
    require("ReachableFolder(cDialogLastPath()) + '/'" in fallback,
        'the Linux and macOS dialog opens in an offline folder')
    win = function(source('Telegram/SourceFiles/platform/win/file_utilities_win.cpp'),
        'bool Get(')
    reach = win.find('ReachableFolder(realLastPath)')
    require(reach != -1 and reach < win.find('dialog.setDirectory(realLastPath)'),
        'the Windows dialog opens in an offline folder')


def emoji_panel_composition():
    # AyuGram #219 (tdesktop #29806): an emoji picked in the Windows emoji
    # panel (Win+.) stayed drawn in the message field until a restart. The
    # panel sends it as an IME commit; the field inserts emoji itself, and
    # unless the open composition is ended first its preedit stays painted.
    body = function(
        source('Telegram/lib_ui/ui/widgets/fields/input_field.cpp'),
        'void InputField::inputMethodEventInner(')
    emoji = body.find('if (Emoji::Find(e->commitString(), nullptr))')
    clear = body.find('_inner->QTextEdit::inputMethodEvent(&clear);', emoji)
    insert = body.find('InputField::insertFromMimeDataInner(&mimeData);', emoji)
    require(emoji != -1 and clear != -1 and insert != -1 and clear < insert
        and 'auto clear = QInputMethodEvent();' in body[emoji:clear],
        'an emoji from the Windows emoji panel leaves its preedit drawn')


def check(root):
    similar_channels()
    links()
    compose_ttl()
    picker_confirmation()
    ttl_media()
    webview()
    sending()
    ghost_send_paths()
    expiring_media()
    rich_forward()
    attach_slot()
    deleted_in_threads(root)
    hidden_join_requests(root)
    hidden_translate_bar()
    deleted_shared_media()
    square_userpic_upload()
    canvas_clamp(root)
    taskbar_icon_shortcuts()
    adaptive_layout_setting()
    tray_counter_logo()
    chat_wallpaper_toggle()
    download_selected(root)
    offline_save_folder(root)
    caption_cancel_restore(root)
    zalgo_script_marks(root)
    media_size_details(root)
    details_mime_fallback(root)
    surrogate_safe_cuts(root)
    details_counts(root)
    details_dc_names(root)
    composer_auto_delete_name(root)
    field_and_mark_cuts(root)
    plugin_and_setting_cuts(root)
    emoji_panel_composition()
    print('AyuGram issue ports PASS: similar channels, links, compose ttl, '
        'picker confirmation, ttl media, webview, sending, ghost send paths, '
        'expiring media, '
        'rich forward, attach slot, deleted in threads, hidden join requests, '
        'hidden translate bar, deleted shared media, square userpic upload, '
        'canvas clamp, taskbar icon shortcuts, adaptive layout setting, '
        'tray counter logo, chat wallpaper toggle, download selected, '
        'offline save folder, emoji panel composition, caption cancel restore, '
        'zalgo script marks, media size details, details mime fallback, surrogate-safe cuts, details counts, details datacenter names, auto-delete toggle name, field and mark cuts, plugin and setting cuts')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='luxury-ayu-ports-') as directory:
        check(Path(directory))
