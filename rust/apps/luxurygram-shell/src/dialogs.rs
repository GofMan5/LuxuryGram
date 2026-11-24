//! The chat list: one row per dialog, virtualized, with search and selection.
//!
//! This is the GPUI-CE counterpart of `Telegram/SourceFiles/dialogs/` (the
//! row layout metrics follow `dialogs.style`: a 62 px row, 10/8 padding, a
//! 19 px unread counter in bold 12 px and a 13 px date). The list owns only
//! presentation; rows arrive from the client through the history bridge or
//! from a synthetic set in the standalone shell, and opening a row calls a
//! handler the host installs, so the list never talks to the network.

use std::{cell::Cell, ops::Range, rc::Rc};

use gpui::{
    App, Bounds, Context, ElementInputHandler, EntityInputHandler, FocusHandle, IntoElement,
    KeyBinding, KeyDownEvent, ListAlignment, ListOffset, ListState, Pixels, Render, SharedString,
    UTF16Selection, Window, actions, canvas, colors::Colors, div, list, prelude::*, px,
    rgb_to_hsla,
};

const DIALOGS_CONTEXT: &str = "LuxuryGramDialogs";

/// Corner radius, design-tokens LUXURY_WATCHER_CARD_RADIUS, as the feed uses.
const RADIUS: f32 = 12.0;

/// Row metrics from `dialogs.style`.
const ROW_HEIGHT: f32 = 62.0;
const AVATAR_SIZE: f32 = 40.0;
const UNREAD_HEIGHT: f32 = 19.0;

/// Upper bound on the search text, the Qt list's filter field limit.
const MAX_QUERY_CHARS: usize = 256;

actions!(dialogs, [OpenSelected, MoveUp, MoveDown, ToggleSearch]);

/// One chat in the list. Mirrors what the Qt `Dialogs::Row` shows; the
/// client fills it from `Data::PeerData` and the history's last message.
#[derive(Clone, Debug, PartialEq)]
pub struct Dialog {
    /// Stable id, the peer's `PeerId` value in the client.
    pub id: u64,
    pub title: String,
    /// The last message's text or a service description, already plain.
    pub subtitle: String,
    /// The last message's time, formatted for display.
    pub time: String,
    pub unread: u32,
    pub pinned: bool,
    pub muted: bool,
    pub online: bool,
    /// True for an outgoing last message, drawn as a check mark's place.
    pub outgoing: bool,
}

/// The rows shown, kept as a snapshot so a repaint never races the client.
#[derive(Clone, Default)]
pub struct DialogStore {
    rows: Rc<Vec<Dialog>>,
}

impl DialogStore {
    pub fn new(rows: Vec<Dialog>) -> Self {
        Self {
            rows: Rc::new(rows),
        }
    }

    pub fn len(&self) -> usize {
        self.rows.len()
    }

    pub fn is_empty(&self) -> bool {
        self.rows.is_empty()
    }

    pub fn get(&self, index: usize) -> Option<&Dialog> {
        self.rows.get(index)
    }

    /// The indices of rows whose title or subtitle contains `query`,
    /// case-insensitively; an empty query lists every row.
    pub fn matching(&self, query: &str) -> Vec<usize> {
        let query = query.trim().to_lowercase();
        if query.is_empty() {
            return (0..self.rows.len()).collect();
        }
        self.rows
            .iter()
            .enumerate()
            .filter(|(_, row)| {
                row.title.to_lowercase().contains(&query)
                    || row.subtitle.to_lowercase().contains(&query)
            })
            .map(|(index, _)| index)
            .collect()
    }
}

/// Receives the dialog the user opened.
pub type OpenHandler = Rc<dyn Fn(u64)>;

pub struct DialogList {
    store: DialogStore,
    /// Indices into the store that pass the current search, in list order.
    visible: Rc<Vec<usize>>,
    list_state: ListState,
    selected: usize,
    query: String,
    search_focus: FocusHandle,
    list_focus: FocusHandle,
    search_open: bool,
    /// The UTF-16 range of the query an input method is still composing.
    marked: Option<Range<usize>>,
    open_handler: Option<OpenHandler>,
    rendered_items: Rc<Cell<usize>>,
}

impl DialogList {
    pub fn new(
        store: DialogStore,
        rendered_items: Rc<Cell<usize>>,
        cx: &mut Context<Self>,
    ) -> Self {
        let visible = store.matching("");
        let list_state = ListState::new(visible.len(), ListAlignment::Top, px(ROW_HEIGHT));
        Self {
            store,
            visible: Rc::new(visible),
            list_state,
            selected: 0,
            query: String::new(),
            search_focus: cx.focus_handle(),
            list_focus: cx.focus_handle(),
            search_open: false,
            marked: None,
            open_handler: None,
            rendered_items,
        }
    }

    /// Replace the rows, keeping the selection on the same chat id when it
    /// is still present, so a refresh never moves the user elsewhere.
    pub fn set_store(&mut self, store: DialogStore, cx: &mut Context<Self>) {
        let selected_id = self.selected_dialog().map(|row| row.id);
        self.store = store;
        self.apply_filter(cx);
        if let Some(id) = selected_id
            && let Some(index) = self
                .visible
                .iter()
                .position(|&i| self.store.get(i).is_some_and(|r| r.id == id))
        {
            self.selected = index;
        } else {
            self.selected = 0;
        }
        cx.notify();
    }

    /// Install the host's "open this chat" path.
    pub fn set_open_handler(&mut self, handler: Option<OpenHandler>, cx: &mut Context<Self>) {
        self.open_handler = handler;
        cx.notify();
    }

    /// The dialog under the selection, or `None` when the list is empty.
    pub fn selected_dialog(&self) -> Option<&Dialog> {
        self.visible
            .get(self.selected)
            .and_then(|&i| self.store.get(i))
    }

    fn apply_filter(&mut self, cx: &mut Context<Self>) {
        let visible = self.store.matching(&self.query);
        self.list_state.reset(visible.len());
        self.selected = self.selected.min(visible.len().saturating_sub(1));
        self.visible = Rc::new(visible);
        cx.notify();
    }

    fn set_query(&mut self, query: String, cx: &mut Context<Self>) {
        self.query = query;
        self.apply_filter(cx);
    }

    fn move_selection(&mut self, delta: i32, cx: &mut Context<Self>) {
        let len = self.visible.len();
        if len == 0 {
            return;
        }
        let next = self.selected as i32 + delta;
        self.selected = next.clamp(0, len as i32 - 1) as usize;
        self.list_state.scroll_to(ListOffset {
            item_ix: self.selected,
            offset_in_item: px(0.),
        });
        cx.notify();
    }

    fn open_selected(&mut self) {
        let Some(handler) = self.open_handler.clone() else {
            return;
        };
        if let Some(row) = self.selected_dialog() {
            handler(row.id);
        }
    }

    fn on_key(&mut self, event: &KeyDownEvent, cx: &mut Context<Self>) {
        let modifiers = event.keystroke.modifiers;
        if modifiers.control || modifiers.platform {
            return;
        }
        match event.keystroke.key.as_str() {
            "enter" => {
                cx.stop_propagation();
                self.open_selected();
            }
            "down" => {
                cx.stop_propagation();
                self.move_selection(1, cx);
            }
            "up" => {
                cx.stop_propagation();
                self.move_selection(-1, cx);
            }
            _ => {}
        }
    }

    fn on_search_key(&mut self, event: &KeyDownEvent, cx: &mut Context<Self>) {
        let modifiers = event.keystroke.modifiers;
        if modifiers.control || modifiers.platform {
            return;
        }
        match event.keystroke.key.as_str() {
            "escape" => {
                cx.stop_propagation();
                self.search_open = false;
                self.set_query(String::new(), cx);
            }
            "backspace" if self.marked.is_none() => {
                cx.stop_propagation();
                self.query.pop();
                let query = self.query.clone();
                self.set_query(query, cx);
            }
            _ => {}
        }
        // Typed text arrives through the platform text input, as in the feed.
    }

    #[cfg(test)]
    pub fn visible_count(&self) -> usize {
        self.visible.len()
    }

    #[cfg(test)]
    pub fn search_focus_for_test(&self) -> &FocusHandle {
        &self.search_focus
    }

    #[cfg(test)]
    pub fn search_open_for_test(&self) -> bool {
        self.search_open
    }

    #[cfg(test)]
    pub fn query_for_test(&self) -> &str {
        &self.query
    }

    /// The query as UTF-16 units, which is what the platform's text input
    /// ranges count.
    fn query_utf16(&self) -> Vec<u16> {
        self.query.encode_utf16().collect()
    }

    /// Replace the UTF-16 `range` (or the composing range, or append) with
    /// `text`, keeping the cursor at the end of the result.
    fn replace_utf16_range(
        &mut self,
        range: Option<Range<usize>>,
        text: &str,
        cx: &mut Context<Self>,
    ) {
        let units = self.query_utf16();
        let (start, end) = match range.or_else(|| self.marked.clone()) {
            Some(range) => (range.start.min(units.len()), range.end.min(units.len())),
            None => (units.len(), units.len()),
        };
        let mut composed = String::from_utf16(&units[..start]).unwrap_or_default();
        composed.push_str(&String::from_utf16(&units[end..]).unwrap_or_default());
        // A search is one line; a pasted line break becomes a space.
        let inserted: String = text
            .chars()
            .map(|c| if c.is_control() { ' ' } else { c })
            .collect();
        composed.push_str(&inserted);
        let query = composed.chars().take(MAX_QUERY_CHARS).collect();
        self.set_query(query, cx);
    }

    /// Toggle the search field, through the list's `ctrl-f` binding. Opening
    /// it moves focus to the field; closing it returns focus to the list.
    fn toggle_search(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.search_open = !self.search_open;
        if !self.search_open {
            self.set_query(String::new(), cx);
            window.focus(&self.list_focus, cx);
        } else {
            window.focus(&self.search_focus, cx);
            cx.notify();
        }
    }
}

/// The platform's text input for the search field: typed characters, dead
/// keys and input-method composition arrive here, not as key events.
impl EntityInputHandler for DialogList {
    fn text_for_range(
        &mut self,
        range: Range<usize>,
        adjusted_range: &mut Option<Range<usize>>,
        _window: &mut Window,
        _cx: &mut Context<Self>,
    ) -> Option<String> {
        let units = self.query_utf16();
        let start = range.start.min(units.len());
        let end = range.end.min(units.len());
        if (start..end) != range {
            *adjusted_range = Some(start..end);
        }
        String::from_utf16(&units[start..end]).ok()
    }

    fn selected_text_range(
        &mut self,
        _ignore_disabled_input: bool,
        _window: &mut Window,
        _cx: &mut Context<Self>,
    ) -> Option<UTF16Selection> {
        // The cursor sits at the end of the query; there is no selection.
        let end = self.query.encode_utf16().count();
        Some(UTF16Selection {
            range: end..end,
            reversed: false,
        })
    }

    fn marked_text_range(
        &self,
        _window: &mut Window,
        _cx: &mut Context<Self>,
    ) -> Option<Range<usize>> {
        self.marked.clone()
    }

    fn unmark_text(&mut self, _window: &mut Window, cx: &mut Context<Self>) {
        self.marked = None;
        cx.notify();
    }

    fn replace_text_in_range(
        &mut self,
        range: Option<Range<usize>>,
        text: &str,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        self.replace_utf16_range(range, text, cx);
        self.marked = None;
    }

    fn replace_and_mark_text_in_range(
        &mut self,
        range: Option<Range<usize>>,
        new_text: &str,
        _new_selected_range: Option<Range<usize>>,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        self.replace_utf16_range(range, new_text, cx);
        // The composition is the tail the input method just wrote.
        let end = self.query.encode_utf16().count();
        let start = end.saturating_sub(new_text.encode_utf16().count());
        self.marked = Some(start..end);
    }

    fn bounds_for_range(
        &mut self,
        _range_utf16: Range<usize>,
        _element_bounds: Bounds<Pixels>,
        _window: &mut Window,
        _cx: &mut Context<Self>,
    ) -> Option<Bounds<Pixels>> {
        // The field draws its text as one line without measurement, so no
        // per-range geometry exists for the candidate window to follow.
        None
    }

    fn character_index_for_point(
        &mut self,
        _point: gpui::Point<Pixels>,
        _window: &mut Window,
        _cx: &mut Context<Self>,
    ) -> Option<usize> {
        None
    }
}

impl Render for DialogList {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let colors = Colors::for_appearance(window);
        let list_colors = colors.clone();
        let store = self.store.clone();
        let visible = Rc::clone(&self.visible);
        let rendered = Rc::clone(&self.rendered_items);
        let selected = self.selected;
        let query = self.query.clone();
        let list_state = self.list_state.clone();

        let search = self.search_open.then(|| {
            div().flex().items_center().px_3().py_2().child(
                div()
                    .id("dialogs-search")
                    .track_focus(&self.search_focus)
                    .on_key_down(cx.listener(|this, event: &KeyDownEvent, _, cx| {
                        this.on_search_key(event, cx);
                    }))
                    .w_full()
                    .px_3()
                    .py_1()
                    .rounded(px(RADIUS))
                    .bg(colors.container)
                    .border_1()
                    .border_color(colors.selected)
                    .text_color(colors.text)
                    .child(if query.is_empty() {
                        "Search chats…".into()
                    } else {
                        SharedString::from(format!("{query}▏"))
                    })
                    // Installs the platform text input on every paint, as
                    // the feed's composer does.
                    .child({
                        let entity = cx.entity();
                        let focus = self.search_focus.clone();
                        canvas(
                            move |_, _, _| (),
                            move |bounds, _, window, cx| {
                                window.handle_input(
                                    &focus,
                                    ElementInputHandler::new(bounds, entity.clone()),
                                    cx,
                                );
                            },
                        )
                        .absolute()
                        .size_full()
                    }),
            )
        });

        div()
            .id("dialog-list")
            .key_context(DIALOGS_CONTEXT)
            .track_focus(&self.list_focus)
            .on_action(cx.listener(|this, _: &ToggleSearch, window, cx| {
                this.toggle_search(window, cx);
            }))
            .on_key_down(cx.listener(|this, event: &KeyDownEvent, _, cx| {
                this.on_key(event, cx);
            }))
            .size_full()
            .flex()
            .flex_col()
            .bg(colors.background)
            .text_color(colors.text)
            .children(search)
            .child(
                div()
                    .flex_1()
                    .min_h_0()
                    .child(list(list_state, move |index, _window, _cx| {
                        rendered.set(rendered.get() + 1);
                        let row_index = visible[index];
                        let Some(row) = store.get(row_index) else {
                            return div().into_any_element();
                        };
                        render_row(row, index == selected, &list_colors)
                    })),
            )
    }
}

/// One row: avatar with initials, title, time, subtitle and unread counter.
fn render_row(row: &Dialog, selected: bool, colors: &Colors) -> gpui::AnyElement {
    let initials: String = row
        .title
        .split_whitespace()
        .filter_map(|word| word.chars().next())
        .take(2)
        .collect::<Vec<_>>()
        .into_iter()
        .collect::<String>()
        .to_uppercase();
    let muted_text = if row.muted {
        rgb_to_hsla(colors.border)
    } else {
        rgb_to_hsla(colors.text)
    };
    div()
        .id(("dialog-row", row.id as usize))
        .flex()
        .items_center()
        .h(px(ROW_HEIGHT))
        .px(px(10.0))
        .py(px(8.0))
        .gap_3()
        .bg(if selected {
            colors.selected
        } else {
            colors.background
        })
        .child(
            div()
                .flex_none()
                .w(px(AVATAR_SIZE))
                .h(px(AVATAR_SIZE))
                .rounded_full()
                .bg(colors.container)
                .border_1()
                .border_color(if row.online {
                    colors.selected
                } else {
                    colors.border
                })
                .flex()
                .items_center()
                .justify_center()
                .text_size(px(14.0))
                .text_color(colors.text)
                .child(SharedString::from(initials)),
        )
        .child(
            div()
                .flex_1()
                .flex()
                .flex_col()
                .justify_center()
                .gap_1()
                .child(
                    div()
                        .flex()
                        .items_center()
                        .justify_between()
                        .child(
                            div()
                                .flex_1()
                                .text_size(px(15.0))
                                .font_weight(gpui::FontWeight::SEMIBOLD)
                                .text_color(colors.text)
                                .truncate()
                                .child(SharedString::from(row.title.clone())),
                        )
                        .child(
                            div()
                                .flex_none()
                                .text_size(px(13.0))
                                .text_color(muted_text)
                                .child(SharedString::from(row.time.clone())),
                        ),
                )
                .child(
                    div()
                        .flex()
                        .items_center()
                        .justify_between()
                        .gap_2()
                        .child(
                            div()
                                .flex_1()
                                .text_size(px(13.0))
                                .text_color(muted_text)
                                .truncate()
                                .child(SharedString::from(row.subtitle.clone())),
                        )
                        .child(if row.unread > 0 {
                            div()
                                .flex_none()
                                .h(px(UNREAD_HEIGHT))
                                .px(px(5.0))
                                .rounded(px(UNREAD_HEIGHT / 2.0))
                                .bg(if row.muted {
                                    colors.container
                                } else {
                                    colors.selected
                                })
                                .flex()
                                .items_center()
                                .text_size(px(12.0))
                                .font_weight(gpui::FontWeight::BOLD)
                                .text_color(colors.background)
                                .child(SharedString::from(row.unread.to_string()))
                                .into_any_element()
                        } else {
                            div().into_any_element()
                        }),
                ),
        )
        .into_any_element()
}

/// Bind the list's key bindings; call once before opening a list.
pub fn init(cx: &mut App) {
    cx.bind_keys([KeyBinding::new(
        "ctrl-f",
        ToggleSearch,
        Some(DIALOGS_CONTEXT),
    )]);
}

/// Open the chat list on its own window, with synthetic rows and an open
/// handler that prints the chosen chat; the standalone `--dialogs-preview`
/// mode, used to see and drive the list without the client.
pub fn run(cx: &mut App) {
    init(cx);
    let rendered = Rc::new(Cell::new(0));
    let list = cx.new(|cx| {
        let mut list =
            DialogList::new(DialogStore::new(build_synthetic_dialogs(200)), rendered, cx);
        list.set_open_handler(
            Some(Rc::new(|id| {
                println!("opened: {id}");
            })),
            cx,
        );
        list
    });
    let bounds = gpui::Bounds::centered(None, gpui::size(px(360.), px(720.)), cx);
    let window_list = list.clone();
    let window = cx
        .open_window(
            gpui::WindowOptions::default()
                .window_bounds(gpui::WindowBounds::Windowed(bounds))
                .titlebar(gpui::TitlebarOptions {
                    title: Some("LuxuryGram — Chats".into()),
                    ..Default::default()
                }),
            move |_window, _cx| window_list.clone(),
        )
        .expect("open the chat list window");
    // The list takes keyboard focus at once, so arrows and Enter act on it
    // without a click.
    window
        .update(cx, |list, window, cx| window.focus(&list.list_focus, cx))
        .expect("focus the list in the fresh window");
    cx.activate(true);
}

/// The synthetic rows the standalone shell shows when the client supplies
/// none, mirroring the feed's synthetic demo in spirit.
pub fn build_synthetic_dialogs(count: usize) -> Vec<Dialog> {
    (0..count)
        .map(|i| Dialog {
            id: 1_000 + i as u64,
            title: match i % 4 {
                0 => format!("Design team {i}"),
                1 => format!("Alice {i}"),
                2 => format!("News channel {i}"),
                _ => format!("Bob {i}"),
            },
            subtitle: match i % 3 {
                0 => "See you tomorrow at ten".into(),
                1 => "photo".into(),
                _ => "The files are uploaded".into(),
            },
            time: format!("{:02}:{:02}", 9 + (i % 12), (i * 7) % 60),
            unread: (i % 5) as u32,
            pinned: i % 11 == 0,
            muted: i % 7 == 0,
            online: i % 3 == 0,
            outgoing: i % 2 == 0,
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use gpui::TestApp;

    fn key(key: &str) -> KeyDownEvent {
        KeyDownEvent {
            keystroke: gpui::Keystroke {
                modifiers: Default::default(),
                key: key.into(),
                key_char: None,
            },
            is_held: false,
            prefer_character_input: false,
        }
    }

    #[test]
    fn search_narrows_the_rows_and_restores_them() {
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| {
            DialogList::new(
                DialogStore::new(build_synthetic_dialogs(50)),
                Rc::new(Cell::new(0)),
                cx,
            )
        });
        window.draw();
        window.update(|list, _, cx| {
            assert_eq!(list.visible_count(), 50);
            list.set_query("alice".into(), cx);
            assert_eq!(list.visible_count(), 13, "titles Alice 1, 5, …");
            assert!(
                list.selected_dialog()
                    .is_some_and(|d| d.title.starts_with("Alice"))
            );
            list.set_query("  ".into(), cx);
            assert_eq!(list.visible_count(), 50);
        });
    }

    #[test]
    fn selection_moves_within_bounds_and_opens_the_chosen_chat() {
        let opened = Rc::new(std::cell::RefCell::new(Vec::<u64>::new()));
        let sink = Rc::clone(&opened);
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| {
            DialogList::new(
                DialogStore::new(build_synthetic_dialogs(5)),
                Rc::new(Cell::new(0)),
                cx,
            )
        });
        window.draw();
        window.update(|list, _, cx| {
            list.set_open_handler(Some(Rc::new(move |id| sink.borrow_mut().push(id))), cx);
            list.on_key(&key("down"), cx);
            list.on_key(&key("down"), cx);
            list.on_key(&key("down"), cx);
            list.on_key(&key("down"), cx);
            list.on_key(&key("down"), cx);
            assert_eq!(list.selected_dialog().map(|d| d.id), Some(1_004));
            list.on_key(&key("up"), cx);
            list.on_key(&key("enter"), cx);
            assert_eq!(*opened.borrow(), vec![1_003]);
        });
    }

    #[test]
    fn refresh_keeps_the_selection_on_the_same_chat() {
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| {
            DialogList::new(
                DialogStore::new(build_synthetic_dialogs(10)),
                Rc::new(Cell::new(0)),
                cx,
            )
        });
        window.draw();
        window.update(|list, _, cx| {
            list.on_key(&key("down"), cx);
            list.on_key(&key("down"), cx);
            assert_eq!(list.selected_dialog().map(|d| d.id), Some(1_002));
            // A new snapshot that prepends a chat: the same id is now third.
            let mut rows = build_synthetic_dialogs(1);
            rows[0].id = 9_999;
            rows.extend(build_synthetic_dialogs(10));
            list.set_store(DialogStore::new(rows), cx);
            assert_eq!(list.selected_dialog().map(|d| d.id), Some(1_002));
        });
    }

    #[test]
    fn search_receives_typed_text_and_input_method_composition() {
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| {
            DialogList::new(
                DialogStore::new(build_synthetic_dialogs(50)),
                Rc::new(Cell::new(0)),
                cx,
            )
        });
        window.draw();
        // Open the search through its binding's handler and focus the field.
        window.update(|list, window, cx| list.toggle_search(window, cx));
        window.draw();
        window.draw();
        window.update(|list, window, cx| {
            EntityInputHandler::replace_text_in_range(list, None, "design", window, cx);
            assert!(list.search_open_for_test());
            assert_eq!(list.visible_count(), 13);
            // An input method composes over the tail, replacing it each step.
            EntityInputHandler::replace_and_mark_text_in_range(list, None, "か", None, window, cx);
            EntityInputHandler::replace_and_mark_text_in_range(
                list, None, "かん", None, window, cx,
            );
            assert_eq!(list.query_for_test(), "designかん");
            assert_eq!(
                EntityInputHandler::marked_text_range(list, window, cx),
                Some(6..8)
            );
            assert_eq!(list.visible_count(), 0);
            EntityInputHandler::unmark_text(list, window, cx);
            assert_eq!(
                EntityInputHandler::marked_text_range(list, window, cx),
                None
            );
        });
        window.draw();
        window.update(|list, window, cx| {
            // Deleting the composition leaves the typed text filtering.
            EntityInputHandler::replace_text_in_range(list, Some(6..8), "", window, cx);
            assert_eq!(list.query_for_test(), "design");
            assert_eq!(list.visible_count(), 13);
        });
    }

    #[test]
    fn list_renders_only_visible_rows() {
        let rendered = Rc::new(Cell::new(0));
        let counter = Rc::clone(&rendered);
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| {
            DialogList::new(
                DialogStore::new(build_synthetic_dialogs(10_000)),
                counter,
                cx,
            )
        });
        window.draw();
        let count = rendered.get();
        assert!(count > 0, "no rows rendered");
        assert!(count < 200, "virtualization failed: rendered {count} rows");
    }
}
