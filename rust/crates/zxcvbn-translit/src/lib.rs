// This file is part of Telegram Desktop,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
// Ported from Telegram/SourceFiles/_other/zxcvbn_translit.cpp and
// Telegram/SourceFiles/ui/passcode_strength_translit.cpp. Behavior, including
// malformed UTF-8 rejection and dropped markup words, is kept byte-for-byte.

//! Transliteration of password-dictionary words for the zxcvbn strength
//! estimator. Pure logic: no I/O and no Qt.

/// One decoded UTF-8 code point and the number of bytes it occupied.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Decoded {
    pub point: char,
    pub length: usize,
}

/// Rejects overlong forms, surrogates, out-of-range code points and truncated
/// sequences exactly like the C++ `DecodeUtf8` helper does.
pub fn decode_utf8(text: &[u8], from: usize) -> Option<Decoded> {
    let byte_at = |index: usize| text.get(index).copied().unwrap_or(0);
    let first = byte_at(from);

    if first < 0x80 {
        return Some(Decoded {
            point: char::from(first),
            length: 1,
        });
    }

    let (length, minimum, mut point) = match first {
        0xC2..=0xDF => (2usize, 0x80u32, u32::from(first & 0x1F)),
        0xE0..=0xEF => (3usize, 0x800u32, u32::from(first & 0x0F)),
        0xF0..=0xF4 => (4usize, 0x10_000u32, u32::from(first & 0x07)),
        _ => return None,
    };

    for offset in 1..length {
        let next = byte_at(from + offset);
        if !(0x80..=0xBF).contains(&next) {
            return None;
        }
        point = (point << 6) | u32::from(next & 0x3F);
    }

    let surrogate = (0xD800..=0xDFFF).contains(&point);
    if point < minimum || surrogate || point > 0x10FFFF {
        return None;
    }

    Some(Decoded {
        point: char::from_u32(point).expect("range checked above"),
        length,
    })
}

/// Cyrillic case folding: U+0400–U+040F, U+0410–U+042F and U+0490.
pub fn lowercase_cyrillic(point: char) -> char {
    let value = u32::from(point);
    if (0x0400..=0x040F).contains(&value) {
        char::from_u32(value + 0x50).unwrap_or(point)
    } else if (0x0410..=0x042F).contains(&value) {
        char::from_u32(value + 0x20).unwrap_or(point)
    } else if value == 0x0490 {
        '\u{0491}'
    } else {
        point
    }
}

const BASIC_LOWER: [&str; 48] = [
    "a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p", "r", "s", "t",
    "u", "f", "kh", "ts", "ch", "sh", "sch", "", "y", "", "e", "yu", "ya", "e", "yo", "dj", "g",
    "ye", "dz", "i", "yi", "j", "lj", "nj", "c", "k", "i", "w", "dz",
];

/// Empty string means a letter that transliterates to nothing; `None` means
/// the code point is not a mapped Cyrillic letter.
pub fn transliterate_cyrillic(lowercase: char) -> Option<&'static str> {
    let value = u32::from(lowercase);
    const FIRST: u32 = 0x0430;
    if (FIRST..FIRST + BASIC_LOWER.len() as u32).contains(&value) {
        Some(BASIC_LOWER[(value - FIRST) as usize])
    } else if value == 0x0491 {
        Some("g")
    } else {
        None
    }
}

/// Returns the mapped word, or `None` if it is not fully mappable or maps to
/// fewer than two characters (the C++ `kMinimumMappedLength`).
pub fn map_word(word: &[u8]) -> Option<String> {
    let mut mapped = String::new();
    let mut from = 0;
    while from < word.len() {
        let decoded = decode_utf8(word, from)?;
        from += decoded.length;
        if decoded.point.is_ascii() {
            mapped.push(decoded.point);
            continue;
        }
        let latin = transliterate_cyrillic(lowercase_cyrillic(decoded.point))?;
        mapped.push_str(latin);
    }
    (mapped.len() >= 2).then_some(mapped)
}

pub fn is_dropped_markup(word: &str) -> bool {
    matches!(word, "chffffff" | "fntahoma" | "b1" | "i0")
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransformError {
    NoWordsMapped,
}

impl std::fmt::Display for TransformError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::NoWordsMapped => formatter.write_str("no words mapped"),
        }
    }
}

impl std::error::Error for TransformError {}

/// Mirrors the original file format: one word per line, first whitespace-
/// delimited token only, CRLF accepted, mapped words emitted one per line.
pub fn transform(input: &[u8]) -> Result<Vec<u8>, TransformError> {
    let mut output = Vec::new();
    let mut written = 0usize;

    for line in input.split(|&byte| byte == b'\n') {
        if line.is_empty() && output.is_empty() && written == 0 && input.ends_with(b"\n") {
            // The final empty segment after a trailing newline is not a line.
            continue;
        }
        let mut word = line;
        if word.last() == Some(&b'\r') {
            word = &word[..word.len() - 1];
        }
        if let Some(end) = word.iter().position(|&byte| byte == b' ' || byte == b'\t') {
            word = &word[..end];
        }
        if let Some(mapped) = map_word(word)
            && !is_dropped_markup(&mapped)
        {
            output.extend_from_slice(mapped.as_bytes());
            output.push(b'\n');
            written += 1;
        }
    }

    if written == 0 {
        Err(TransformError::NoWordsMapped)
    } else {
        Ok(output)
    }
}

/// Same result as `transform`, but reuses one scratch buffer for mapped words
/// and works on bytes throughout. Short ASCII words need no heap allocation,
/// which matters for million-word dictionaries.
pub fn transform_optimized(input: &[u8]) -> Result<Vec<u8>, TransformError> {
    let mut output = Vec::with_capacity(input.len());
    let mut mapped = Vec::with_capacity(64);
    let mut pos = 0usize;
    let mut written = 0usize;

    while pos < input.len() {
        let line_end = input[pos..]
            .iter()
            .position(|&byte| byte == b'\n')
            .map_or(input.len(), |offset| pos + offset);
        let next = if line_end == input.len() {
            input.len()
        } else {
            line_end + 1
        };
        let mut word = &input[pos..line_end];
        pos = next;

        if word.last() == Some(&b'\r') {
            word = &word[..word.len() - 1];
        }
        if let Some(end) = word.iter().position(|&byte| byte == b' ' || byte == b'\t') {
            word = &word[..end];
        }
        if word.is_empty() {
            continue;
        }

        mapped.clear();
        let mut from = 0usize;
        let mut mappable = true;
        while from < word.len() {
            let Some(decoded) = decode_utf8(word, from) else {
                mappable = false;
                break;
            };
            from += decoded.length;
            if decoded.point.is_ascii() {
                mapped.push(decoded.point as u8);
                continue;
            }
            let Some(latin) = transliterate_cyrillic(lowercase_cyrillic(decoded.point)) else {
                mappable = false;
                break;
            };
            mapped.extend_from_slice(latin.as_bytes());
        }

        if mappable
            && mapped.len() >= 2
            && !matches!(mapped.as_slice(), b"chffffff" | b"fntahoma" | b"b1" | b"i0")
        {
            output.extend_from_slice(&mapped);
            output.push(b'\n');
            written += 1;
        }
    }

    if written == 0 {
        Err(TransformError::NoWordsMapped)
    } else {
        Ok(output)
    }
}

/// Minimal binary C ABI for hosts that cannot afford a JSON round trip.
/// Enabled with the `ffi` feature; the pure functions above are unchanged.
#[cfg(feature = "ffi")]
mod ffi {
    use crate::transform_optimized;

    /// Transform a whole dictionary in one call.
    ///
    /// # Safety
    /// `input` must be valid for `len` bytes and `out_len` must point to a
    /// writable `usize`. The returned buffer belongs to the caller and must be
    /// released with `luxury_zxcvbn_free`. A null result means no word mapped
    /// (the C++ CLI reports that as an error), or invalid arguments.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn luxury_zxcvbn_transform(
        input: *const u8,
        len: usize,
        out_len: *mut usize,
    ) -> *mut u8 {
        if input.is_null() || out_len.is_null() {
            return std::ptr::null_mut();
        }
        let bytes = unsafe { std::slice::from_raw_parts(input, len) };
        match transform_optimized(bytes) {
            Ok(output) => {
                let boxed = output.into_boxed_slice();
                unsafe { *out_len = boxed.len() };
                Box::into_raw(boxed) as *mut u8
            }
            Err(_) => {
                unsafe { *out_len = 0 };
                std::ptr::null_mut()
            }
        }
    }

    /// # Safety
    /// `ptr` must come from `luxury_zxcvbn_transform` with the same `len`.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn luxury_zxcvbn_free(ptr: *mut u8, len: usize) {
        if !ptr.is_null() && len > 0 {
            unsafe {
                drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(ptr, len)));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn maps_basic_cyrillic() {
        assert_eq!(map_word("Привет".as_bytes()).as_deref(), Some("privet"));
        assert_eq!(map_word("МИР".as_bytes()).as_deref(), Some("mir"));
        assert_eq!(map_word("Щука".as_bytes()).as_deref(), Some("schuka"));
    }

    #[test]
    fn maps_ghe_with_descender_and_empty_letters() {
        assert_eq!(map_word("Ёлка".as_bytes()).as_deref(), Some("yolka"));
        assert_eq!(map_word("Ґа".as_bytes()).as_deref(), Some("ga"));
        assert_eq!(map_word("Ъя".as_bytes()).as_deref(), Some("ya"));
    }

    #[test]
    fn rejects_short_and_unmapped_words() {
        assert_eq!(map_word(b"a"), None);
        assert_eq!(map_word("а".as_bytes()), None);
        assert_eq!(map_word("я".as_bytes()).as_deref(), Some("ya"));
        assert_eq!(map_word("日本語".as_bytes()), None);
        assert_eq!(map_word(&[0xC0, 0x80]), None);
        assert_eq!(map_word(&[0xE2, 0x82]), None);
    }

    #[test]
    fn drops_markup_words() {
        assert!(is_dropped_markup("chffffff"));
        assert!(is_dropped_markup("b1"));
        assert!(!is_dropped_markup("privet"));
    }

    #[test]
    fn transforms_lines_with_crlf_and_tabs() {
        let input = "ПРИВЕТ\r\nМир\tignored\n  \n\r\n".as_bytes();
        assert_eq!(transform(input).unwrap(), b"privet\nmir\n");
        assert_eq!(transform_optimized(input).unwrap(), b"privet\nmir\n");
    }

    #[test]
    fn optimized_matches_transform() {
        let cases: [&[u8]; 9] = [
            b"",
            b"a\n",
            b"hello\nworld\n",
            "Привет\r\nМир\tignored\n  \n\r\n".as_bytes(),
            "Ёлка Ґа\n日本語\n".as_bytes(),
            b"\xC0\x80\n",
            b"chffffff\nfntahoma\nb1\ni0\n",
            b"one two three\n\nfour",
            b"the                124933855\r\nof                 59700086\n",
        ];
        for case in cases {
            assert_eq!(transform(case), transform_optimized(case), "case: {case:?}");
        }
    }

    #[test]
    fn errors_when_nothing_was_mapped() {
        assert_eq!(
            transform(b"chffffff\nb1\nx\n"),
            Err(TransformError::NoWordsMapped)
        );
        assert_eq!(transform(b""), Err(TransformError::NoWordsMapped));
    }
}
