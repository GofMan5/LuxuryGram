//! Message model for the H1 virtualized feed slice.
//!
//! Behavior follows Telegram Desktop's history view: date separators change
//! with the calendar day (`recountDisplayDateInBlocks`) and consecutive
//! messages from one sender are grouped within 15 minutes
//! (`computeIsAttachToPrevious`, kAttachMessageToPreviousSecondsDelta = 900).

pub const ATTACH_SECONDS: u64 = 900;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EntityKind {
    Bold,
    Italic,
    Link,
    Code,
}

#[derive(Debug, Clone)]
pub struct EntitySpan {
    pub start: usize,
    pub end: usize,
    pub kind: EntityKind,
}

#[derive(Debug, Clone)]
pub struct Message {
    pub id: usize,
    pub sender: String,
    pub text: String,
    pub entities: Vec<EntitySpan>,
    pub timestamp: u64,
    pub outgoing: bool,
    pub date_label: String,
    pub show_date_header: bool,
    pub first_in_group: bool,
}

fn span(text: &str, needle: &str, kind: EntityKind) -> Option<EntitySpan> {
    text.find(needle).map(|start| EntitySpan {
        start,
        end: start + needle.len(),
        kind,
    })
}

/// Deterministic synthetic history, shared by the feed view and its tests.
pub fn build_synthetic_messages(count: usize) -> Vec<Message> {
    const SENDERS: [&str; 6] = [
        "Alice",
        "Bob",
        "Charlie",
        "LuxuryGram Channel",
        "Diana",
        "Eve",
    ];
    const SNIPPETS: [&str; 5] = [
        "Short ping check.",
        "Benchmarks need deterministic text runs that wrap across several lines.",
        "MTProto update stream and virtualized GPUI-CE list scroll under 10k history items.",
        "Variable-height bubbles with sender header, styled entity runs and trailing timestamp.",
        "https://luxurygram.example/thread/42 — mixed Cyrillic text: Привет, тест рендера.",
    ];
    let base = 1_800_000_000u64;

    (0..count)
        .map(|ix| {
            let sender = SENDERS[ix % SENDERS.len()].to_string();
            let snippet = SNIPPETS[ix % SNIPPETS.len()];
            let text = format!("#{ix:06} {snippet}");
            let mut entities = Vec::new();
            if let Some(s) = span(&text, "MTProto", EntityKind::Bold) {
                entities.push(s);
            }
            if let Some(s) = span(&text, "GPUI-CE", EntityKind::Code) {
                entities.push(s);
            }
            if let Some(s) = span(
                &text,
                "https://luxurygram.example/thread/42",
                EntityKind::Link,
            ) {
                entities.push(s);
            }
            if let Some(s) = span(&text, "Привет", EntityKind::Italic) {
                entities.push(s);
            }
            Message {
                id: ix + 1,
                sender,
                text,
                entities,
                timestamp: base - (count - ix) as u64 * 60,
                outgoing: ix % 3 == 0,
                date_label: String::new(),
                show_date_header: false,
                first_in_group: true,
            }
        })
        .collect()
}

/// Fill date labels and grouping flags in place.
pub fn apply_grouping(messages: &mut [Message]) {
    let mut last_day = None;
    let mut last_sender: Option<&str> = None;
    let mut last_time = 0u64;

    for message in messages.iter_mut() {
        let day = message.timestamp / 86_400;
        message.show_date_header = Some(day) != last_day;
        if message.show_date_header {
            message.date_label = format!("Day {day}");
            last_day = Some(day);
        }
        message.first_in_group = last_sender != Some(message.sender.as_str())
            || message.timestamp.saturating_sub(last_time) > ATTACH_SECONDS;
        last_sender = Some(&message.sender);
        last_time = message.timestamp;
    }
}

pub fn formatted_time(timestamp: u64) -> String {
    let seconds = timestamp % 86_400;
    format!("{:02}:{:02}", seconds / 3_600, (seconds / 60) % 60)
}
