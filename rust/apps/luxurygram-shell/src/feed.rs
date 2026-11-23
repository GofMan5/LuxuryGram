//! H1 UI slice: virtualized message feed on GPUI-CE 0.3.0.
//!
//! Uses `list()` + `ListState` with `ListAlignment::Bottom` and
//! `FollowMode::Tail`, mirroring a chat log. Rows are built only for the
//! visible range plus overdraw.

use std::{cell::Cell, rc::Rc};

use gpui::{
    App, AppContext, Context, FollowMode, HighlightStyle, IntoElement, ListAlignment, ListState,
    Render, SharedString, StyledText, UnderlineStyle, Window, colors::Colors, div, list,
    prelude::*, px, rgb_to_hsla,
};

use crate::message::{EntityKind, apply_grouping, build_synthetic_messages, formatted_time};

const RADIUS: f32 = 12.0; // design-tokens LUXURY_WATCHER_CARD_RADIUS

pub struct MessageFeed {
    messages: Vec<crate::message::Message>,
    list_state: ListState,
    rendered_items: Rc<Cell<usize>>,
}

impl MessageFeed {
    pub fn new(message_count: usize, rendered_items: Rc<Cell<usize>>) -> Self {
        let mut messages = build_synthetic_messages(message_count);
        apply_grouping(&mut messages);
        let list_state = ListState::new(messages.len(), ListAlignment::Bottom, px(240.0));
        list_state.set_follow_mode(FollowMode::Tail);
        Self {
            messages,
            list_state,
            rendered_items,
        }
    }

    #[cfg(test)]
    pub fn message_count(&self) -> usize {
        self.messages.len()
    }

    #[cfg(test)]
    pub fn rendered_items(&self) -> usize {
        self.rendered_items.get()
    }

    #[cfg(test)]
    pub fn scroll_to_bottom(&mut self) {
        self.list_state.scroll_to_end();
        self.list_state.set_follow_mode(FollowMode::Tail);
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
    fn render(&mut self, window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
        let colors = Colors::for_appearance(window);
        let messages = self.messages.clone();
        let rendered = Rc::clone(&self.rendered_items);

        div()
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
                    .child("LuxuryGram — message feed (H1)"),
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
    let bounds = gpui::Bounds::centered(None, gpui::size(px(960.), px(640.)), cx);
    cx.open_window(
        gpui::WindowOptions::default()
            .window_bounds(gpui::WindowBounds::Windowed(bounds))
            .titlebar(gpui::TitlebarOptions {
                title: Some("LuxuryGram — Message Feed".into()),
                ..Default::default()
            }),
        |_window, cx| cx.new(|_cx| MessageFeed::new(10_000, Rc::new(Cell::new(0)))),
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
        let mut window = app.open_window(|_, _| MessageFeed::new(10_000, counter));
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
        let mut window = app.open_window(|_, _| MessageFeed::new(1_000, counter));
        window.draw();
        window.update(|feed, _, _| {
            feed.list_state.scroll_by(px(-500.0));
            assert!(!feed.list_state.is_following_tail());
            feed.scroll_to_bottom();
            assert!(feed.list_state.is_following_tail());
        });
    }
}
