//! Read-only adapter for a single chat exported as JSON by Telegram Desktop.

use std::{
    fs::File,
    io::{self, Read},
    path::Path,
};

use serde_json::Value;

use luxurygram_shell::message::Message;

// ponytail: the prototype loads one small snapshot in memory; use streaming
// and paging when larger histories need to be opened.
const MAX_BYTES: u64 = 16 * 1024 * 1024;

fn invalid(reason: &'static str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, reason)
}

pub fn load(path: &Path) -> io::Result<Vec<Message>> {
    if !path.metadata()?.is_file() {
        return Err(invalid("expected a regular file"));
    }
    let mut bytes = Vec::new();
    File::open(path)?
        .take(MAX_BYTES + 1)
        .read_to_end(&mut bytes)?;
    if bytes.len() as u64 > MAX_BYTES {
        return Err(invalid("the export exceeds 16 MiB"));
    }
    parse(&bytes)
}

fn parse(bytes: &[u8]) -> io::Result<Vec<Message>> {
    let root: Value = serde_json::from_slice(bytes).map_err(|_| invalid("invalid JSON"))?;
    let items = root
        .get("messages")
        .and_then(Value::as_array)
        .filter(|_| root.get("id").and_then(Value::as_u64).is_some())
        .ok_or_else(|| invalid("expected a single-chat JSON export, not an account archive"))?;

    let mut messages = Vec::new();
    for item in items {
        if item["type"].as_str() != Some("message") || item.get("rich_message").is_some() {
            continue;
        }
        let text = match &item["text"] {
            Value::String(text) => text.clone(),
            Value::Array(parts) => parts
                .iter()
                .map(|part| {
                    part.as_str()
                        .or_else(|| part["text"].as_str())
                        .ok_or_else(|| invalid("invalid message text part"))
                })
                .collect::<io::Result<String>>()?,
            _ => return Err(invalid("invalid message text")),
        };
        if text.is_empty() {
            continue;
        }
        let id = item["id"]
            .as_u64()
            .and_then(|id| usize::try_from(id).ok())
            .ok_or_else(|| invalid("invalid message id"))?;
        let timestamp = item["date_unixtime"]
            .as_str()
            .and_then(|date| date.parse().ok())
            .ok_or_else(|| invalid("invalid message timestamp"))?;
        messages.push(Message {
            id,
            sender: item["from"].as_str().unwrap_or("Unknown sender").to_owned(),
            text,
            entities: Vec::new(),
            timestamp,
            outgoing: false,
            date_label: String::new(),
            show_date_header: false,
            first_in_group: true,
        });
    }
    messages.sort_by_key(|message| (message.timestamp, message.id));
    if messages.is_empty() {
        return Err(invalid("the export contains no supported text messages"));
    }
    Ok(messages)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_single_chat_shape_and_preserves_mixed_text() {
        let messages = parse(br#"{"name":"Chat","id":9,"type":"personal_chat","messages":[
            {"id":3,"type":"service","date_unixtime":"30"},
            {"id":2,"type":"message","from":"Bob","date_unixtime":"20","text":"new"},
            {"id":1,"type":"message","from":"Alice","date_unixtime":"10","text":["old ",{"type":"bold","text":"text"}]}
        ]}"#).unwrap();
        assert_eq!(
            messages
                .iter()
                .map(|message| message.id)
                .collect::<Vec<_>>(),
            [1, 2]
        );
        assert_eq!(messages[0].text, "old text");
        assert_eq!(messages[0].sender, "Alice");
    }

    #[test]
    fn refuses_account_archives_and_malformed_json() {
        assert!(parse(br#"{"chats":{"list":[]}}"#).is_err());
        assert!(parse(b"{").is_err());
    }
}
