//! H1 UI slice: virtualized message feed on GPUI-CE 0.3.0.
//!
//! Uses `list()` + `ListState` with `ListAlignment::Bottom` and
//! `FollowMode::Tail`, mirroring a chat log. Rows are built only for the
//! visible range plus overdraw. Search filters the same list in place.

use std::{cell::Cell, rc::Rc};

use gpui::{
    App, AppContext, Context, FocusHandle, FollowMode, HighlightStyle, IntoElement, KeyBinding,
    KeyDownEvent, ListAlignment, ListState, Render, SharedString, StyledText, UnderlineStyle,
    Window, actions, colors::Colors, div, list, prelude::*, px, rgb_to_hsla,
};

use crate::message::{
    EntityKind, MessageStore, apply_grouping, build_synthetic_messages, formatted_time,
};

const RADIUS: f32 = 12.0; // design-tokens LUXURY_WATCHER_CARD_RADIUS
const FEED_CONTEXT: &str = "LuxuryGramFeed";

actions!(feed, [ToggleSearch, CloseSearch, ScrollToBottom]);

pub struct MessageFeed {
    store: MessageStore,
    messages: Rc<Vec<crate::message::Message>>,
    list_state: ListState,
    rendered_items: Rc<Cell<usize>>,
    search_open: bool,
    query: String,
    search_focus: FocusHandle,
}

impl MessageFeed {
    pub fn new(
        message_count: usize,
        rendered_items: Rc<Cell<usize>>,
        cx: &mut Context<Self>,
    ) -> Self {
        Self::from_store(
            MessageStore::new(build_synthetic_messages(message_count)),
            rendered_items,
            cx,
        )
    }

    pub fn from_store(
        store: MessageStore,
        rendered_items: Rc<Cell<usize>>,
        cx: &mut Context<Self>,
    ) -> Self {
        let messages = store.snapshot();
        let list_state = ListState::new(messages.len(), ListAlignment::Bottom, px(240.0));
        list_state.set_follow_mode(FollowMode::Tail);
        Self {
            store,
            messages,
            list_state,
            rendered_items,
            search_open: false,
            query: String::new(),
            search_focus: cx.focus_handle(),
        }
    }

    #[cfg(test)]
    pub fn message_count(&self) -> usize {
        self.messages.len()
    }

    /// Number of row bodies built in the last frame; used by tests and benches
    /// to prove the list renders a viewport, not the whole history.
    pub fn rendered_items(&self) -> usize {
        self.rendered_items.get()
    }

    pub fn reset_render_counter(&self) {
        self.rendered_items.set(0);
    }

    #[cfg(test)]
    pub fn query(&self) -> &str {
        &self.query
    }

    #[cfg(test)]
    pub fn search_open(&self) -> bool {
        self.search_open
    }

    fn set_query(&mut self, query: String, cx: &mut Context<Self>) {
        self.query = query;
        let needle = self.query.to_lowercase();
        self.messages = if needle.is_empty() {
            self.store.snapshot()
        } else {
            let mut filtered: Vec<_> = self
                .store
                .messages()
                .iter()
                .filter(|message| message.text.to_lowercase().contains(&needle))
                .cloned()
                .collect();
            apply_grouping(&mut filtered);
            Rc::new(filtered)
        };
        self.list_state.reset(self.messages.len());
        if !self.messages.is_empty() {
            self.list_state.scroll_to_reveal_item(0);
        }
        cx.notify();
    }

    fn open_search(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.search_open = true;
        window.focus(&self.search_focus, cx);
        cx.notify();
    }

    fn close_search(&mut self, cx: &mut Context<Self>) {
        self.search_open = false;
        self.set_query(String::new(), cx);
    }

    #[cfg(test)]
    pub fn scroll_to_bottom(&mut self) {
        self.list_state.scroll_to_end();
        self.list_state.set_follow_mode(FollowMode::Tail);
    }

    /// Scroll by a pixel delta; used by the wheel handler and the scroll bench.
    pub fn scroll_by(&mut self, pixels: f32) {
        self.list_state.scroll_by(px(pixels));
    }

    fn on_search_key(&mut self, event: &KeyDownEvent, cx: &mut Context<Self>) {
        let key = event.keystroke.key.as_str();
        if event.keystroke.modifiers.control || event.keystroke.modifiers.platform {
            return;
        }
        if key == "escape" {
            self.close_search(cx);
        } else if key == "backspace" {
            self.query.pop();
            let query = self.query.clone();
            self.set_query(query, cx);
        } else if let Some(character) = event
            .keystroke
            .key_char
            .as_ref()
            .filter(|value| !value.is_empty() && !value.chars().any(char::is_control))
        {
            self.query.push_str(character);
            let query = self.query.clone();
            self.set_query(query, cx);
        }
    }

    fn render_styled_text(message: &crate::message::Message, colors: &Colors) -> StyledText {
        let mut highlights = Vec::new();
        for entity in &message.entities {
            let style = match entity.kind {
                EntityKind::Bold => HighlightStyle {
                    font_weight: Some(gpui::FontWeight::BOLD),
                    ..Default::default()
                },
                EntityKind::Italic => HighlightStyle {
                    font_style: Some(gpui::FontStyle::Italic),
                    ..Default::default()
                },
                EntityKind::Code => HighlightStyle {
                    background_color: Some(rgb_to_hsla(colors.container)),
                    ..Default::default()
                },
                EntityKind::Link => HighlightStyle {
                    color: Some(rgb_to_hsla(colors.selected)),
                    underline: Some(UnderlineStyle {
                        thickness: px(1.0),
                        color: Some(rgb_to_hsla(colors.selected)),
                        wavy: false,
                    }),
                    ..Default::default()
                },
            };
            highlights.push((entity.start..entity.end, style));
        }
        highlights.sort_by_key(|(range, _)| range.start);
        StyledText::new(SharedString::from(message.text.clone())).with_highlights(highlights)
    }
}

impl Render for MessageFeed {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let colors = Colors::for_appearance(window);
        let messages = Rc::clone(&self.messages);
        let rendered = Rc::clone(&self.rendered_items);
        let search_open = self.search_open;
        let query = self.query.clone();

        let top_bar = if search_open {
            div()
                .flex()
                .items_center()
                .gap_2()
                .child(
                    div()
                        .id("feed-search")
                        .track_focus(&self.search_focus)
                        .on_key_down(cx.listener(|this, event: &KeyDownEvent, _, cx| {
                            this.on_search_key(event, cx);
                        }))
                        .min_w(px(240.0))
                        .px_3()
                        .py_1()
                        .rounded(px(RADIUS))
                        .bg(colors.container)
                        .border_1()
                        .border_color(colors.selected)
                        .text_color(colors.text)
                        .child(if query.is_empty() {
                            "Search messages…".into()
                        } else {
                            SharedString::from(format!("{query}▏"))
                        }),
                )
                .child(
                    div()
                        .id("feed-close-search")
                        .on_click(cx.listener(|this, _, _, cx| this.close_search(cx)))
                        .px_2()
                        .py_1()
                        .text_sm()
                        .text_color(colors.disabled)
                        .child("Esc"),
                )
        } else {
            div()
                .flex()
                .items_center()
                .child("LuxuryGram — message feed (Ctrl+F to search)")
        };

        div()
            .id("message-feed")
            .key_context(FEED_CONTEXT)
            .on_action(cx.listener(|this, _: &ToggleSearch, window, cx| {
                this.open_search(window, cx);
            }))
            .on_action(cx.listener(|this, _: &CloseSearch, _, cx| {
                this.close_search(cx);
            }))
            .on_action(cx.listener(|this, _: &ScrollToBottom, _, cx| {
                this.list_state.scroll_to_end();
                this.list_state.set_follow_mode(FollowMode::Tail);
                cx.notify();
            }))
            .size_full()
            .flex()
            .flex_col()
            .bg(colors.background)
            .text_color(colors.text)
            .child(
                div()
                    .flex_none()
                    .h(px(48.0))
                    .px_4()
                    .flex()
                    .items_center()
                    .border_b_1()
                    .border_color(colors.border)
                    .child(top_bar),
            )
            .child(
                div()
                    .flex_1()
                    .min_h_0()
                    .child(list(self.list_state.clone(), move |ix, _window, _cx| {
                        rendered.set(rendered.get() + 1);
                        let message = &messages[ix];
                        if message.show_date_header {
                            return div()
                                .w_full()
                                .py_2()
                                .flex()
                                .justify_center()
                                .child(
                                    div()
                                        .px_3()
                                        .py_1()
                                        .rounded_full()
                                        .bg(colors.container)
                                        .text_xs()
                                        .text_color(colors.disabled)
                                        .child(SharedString::from(message.date_label.clone())),
                                )
                                .into_any_element();
                        }
                        div()
                            .id(("message", message.id))
                            .w_full()
                            .px_4()
                            .py_1()
                            .flex()
                            .when(message.outgoing, |el| el.justify_end())
                            .child(
                                div()
                                    .max_w(px(520.0))
                                    .px_3()
                                    .py_2()
                                    .rounded(px(RADIUS))
                                    .bg(if message.outgoing {
                                        colors.selected
                                    } else {
                                        colors.container
                                    })
                                    .flex()
                                    .flex_col()
                                    .gap_1()
                                    .when(!message.outgoing && message.first_in_group, |el| {
                                        el.child(
                                            div()
                                                .text_xs()
                                                .text_color(colors.disabled)
                                                .child(SharedString::from(message.sender.clone())),
                                        )
                                    })
                                    .child(Self::render_styled_text(message, &colors))
                                    .child(
                                        div()
                                            .flex()
                                            .justify_end()
                                            .text_xs()
                                            .text_color(colors.disabled)
                                            .child(formatted_time(message.timestamp)),
                                    ),
                            )
                            .into_any_element()
                    }))
                    .size_full(),
            )
            .into_any_element()
    }
}

pub fn run(cx: &mut App) {
    cx.bind_keys([
        KeyBinding::new("ctrl-f", ToggleSearch, Some(FEED_CONTEXT)),
        KeyBinding::new("end", ScrollToBottom, Some(FEED_CONTEXT)),
    ]);
    let bounds = gpui::Bounds::centered(None, gpui::size(px(960.), px(640.)), cx);
    cx.open_window(
        gpui::WindowOptions::default()
            .window_bounds(gpui::WindowBounds::Windowed(bounds))
            .titlebar(gpui::TitlebarOptions {
                title: Some("LuxuryGram — Message Feed".into()),
                ..Default::default()
            }),
        |_window, cx| cx.new(|cx| MessageFeed::new(10_000, Rc::new(Cell::new(0)), cx)),
    )
    .expect("open the feed window");
    cx.activate(true);
}

#[cfg(test)]
mod tests {
    use super::*;
    use gpui::TestApp;

    #[test]
    fn list_renders_only_visible_rows() {
        let rendered = Rc::new(Cell::new(0));
        let counter = Rc::clone(&rendered);
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| MessageFeed::new(10_000, counter, cx));
        window.draw();

        window.read(|feed, _| {
            assert_eq!(feed.message_count(), 10_000);
            let count = feed.rendered_items();
            assert!(count > 0, "no rows rendered");
            assert!(count < 200, "virtualization failed: rendered {count} rows");
        });
    }

    #[test]
    fn scroll_to_bottom_restores_tail_following() {
        let rendered = Rc::new(Cell::new(0));
        let counter = Rc::clone(&rendered);
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| MessageFeed::new(1_000, counter, cx));
        window.draw();
        window.update(|feed, _, _| {
            feed.list_state.scroll_by(px(-500.0));
            assert!(!feed.list_state.is_following_tail());
            feed.scroll_to_bottom();
            assert!(feed.list_state.is_following_tail());
        });
    }

    #[test]
    fn search_filters_messages_and_clear_restores_all() {
        let rendered = Rc::new(Cell::new(0));
        let counter = Rc::clone(&rendered);
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| MessageFeed::new(1_000, counter, cx));
        window.draw();

        window.update(|feed, _, cx| {
            feed.search_open = true;
            feed.set_query("GPUI-CE".into(), cx);
        });
        window.draw();
        window.read(|feed, _| {
            assert!(feed.search_open());
            assert_eq!(feed.query(), "GPUI-CE");
            assert!(feed.message_count() > 0 && feed.message_count() < 1_000);
        });

        window.update(|feed, _, cx| feed.close_search(cx));
        window.draw();
        window.read(|feed, _| {
            assert!(!feed.search_open());
            assert_eq!(feed.query(), "");
            assert_eq!(feed.message_count(), 1_000);
        });
    }

    #[test]
    fn supplied_history_renders_filters_and_restores_shared_snapshot() {
        let mut app = TestApp::new();
        let mut input = build_synthetic_messages(2);
        input[0].text = "Привет из внешней истории".into();
        input[0].entities.clear();
        let store = MessageStore::new(input);
        let snapshot = store.snapshot();
        let mut window =
            app.open_window(|_, cx| MessageFeed::from_store(store, Rc::new(Cell::new(0)), cx));
        window.draw();
        window.read(|feed, _| {
            assert!(Rc::ptr_eq(&feed.messages, &snapshot));
            assert_eq!(feed.rendered_items(), 2);
        });
        window.update(|feed, _, cx| feed.set_query("ВНЕШНЕЙ".into(), cx));
        window.draw();
        window.read(|feed, _| {
            assert_eq!(feed.message_count(), 1);
            assert_eq!(feed.messages[0].id, snapshot[0].id);
        });
        window.update(|feed, _, cx| feed.close_search(cx));
        window.read(|feed, _| assert!(Rc::ptr_eq(&feed.messages, &snapshot)));
    }

    #[test]
    fn search_backspace_and_escape_update_query() {
        let rendered = Rc::new(Cell::new(0));
        let counter = Rc::clone(&rendered);
        let mut app = TestApp::new();
        let mut window = app.open_window(|_, cx| MessageFeed::new(200, counter, cx));
        window.draw();

        let mut backspace = KeyDownEvent {
            keystroke: gpui::Keystroke {
                modifiers: Default::default(),
                key: "backspace".into(),
                key_char: None,
            },
            is_held: false,
            prefer_character_input: false,
        };
        window.update(|feed, _, cx| {
            feed.search_open = true;
            feed.set_query("abc".into(), cx);
            feed.on_search_key(&backspace, cx);
        });
        window.read(|feed, _| assert_eq!(feed.query(), "ab"));

        let escape = KeyDownEvent {
            keystroke: gpui::Keystroke {
                modifiers: Default::default(),
                key: "escape".into(),
                key_char: None,
            },
            is_held: false,
            prefer_character_input: false,
        };
        backspace = escape;
        window.update(|feed, _, cx| feed.on_search_key(&backspace, cx));
        window.read(|feed, _| {
            assert!(!feed.search_open());
            assert!(feed.query().is_empty());
        });
    }
}
