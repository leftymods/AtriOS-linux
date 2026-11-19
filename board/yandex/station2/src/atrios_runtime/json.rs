use std::collections::BTreeMap;
use std::fmt::Write as _;

#[derive(Clone, Debug, PartialEq)]
pub enum Json {
    Null,
    Bool(bool),
    Num(f64),
    Str(String),
    Arr(Vec<Json>),
    Obj(BTreeMap<String, Json>),
}

impl Json {
    pub fn as_str(&self) -> Option<&str> {
        match self {
            Json::Str(s) => Some(s.as_str()),
            _ => None,
        }
    }

    pub fn as_bool(&self) -> Option<bool> {
        match self {
            Json::Bool(b) => Some(*b),
            Json::Num(n) => Some(*n != 0.0),
            _ => None,
        }
    }

    pub fn as_f64(&self) -> Option<f64> {
        match self {
            Json::Num(n) if n.is_finite() => Some(*n),
            _ => None,
        }
    }

    pub fn as_i64(&self) -> Option<i64> {
        match self {
            Json::Num(n) if n.is_finite() => Some(*n as i64),
            Json::Str(s) => s.trim().parse::<i64>().ok(),
            _ => None,
        }
    }

    pub fn as_obj(&self) -> Option<&BTreeMap<String, Json>> {
        match self {
            Json::Obj(m) => Some(m),
            _ => None,
        }
    }

    pub fn as_arr(&self) -> Option<&[Json]> {
        match self {
            Json::Arr(a) => Some(a.as_slice()),
            _ => None,
        }
    }

    pub fn get(&self, key: &str) -> Option<&Json> {
        self.as_obj()?.get(key)
    }

    pub fn get_str(&self, key: &str) -> Option<&str> {
        self.get(key)?.as_str()
    }

    pub fn parse(input: &str) -> Result<Json, String> {
        let mut p = Parser {
            bytes: input.as_bytes(),
            pos: 0,
        };
        p.skip_ws();
        let val = p.parse_value(0)?;
        p.skip_ws();
        if p.pos != p.bytes.len() {
            return Err("Trailing characters after JSON".into());
        }
        Ok(val)
    }

    pub fn to_string_compact(&self) -> String {
        let mut out = String::new();
        self.write_to(&mut out, None, 0);
        out
    }

    pub fn to_string_pretty(&self) -> String {
        let mut out = String::new();
        self.write_to(&mut out, Some(2), 0);
        out
    }

    fn write_to(&self, out: &mut String, indent: Option<usize>, depth: usize) {
        match self {
            Json::Null => out.push_str("null"),
            Json::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            Json::Num(n) => {
                if !n.is_finite() {
                    out.push_str("null");
                } else if n.fract() == 0.0 && *n >= (i64::MIN as f64) && *n <= (i64::MAX as f64) {
                    let _ = write!(out, "{}", *n as i64);
                } else {
                    let _ = write!(out, "{}", n);
                }
            }
            Json::Str(s) => write_escaped_str(out, s),
            Json::Arr(arr) => {
                if arr.is_empty() {
                    out.push_str("[]");
                    return;
                }
                out.push('[');
                for (i, item) in arr.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    if let Some(sp) = indent {
                        out.push('\n');
                        for _ in 0..(depth + 1) * sp {
                            out.push(' ');
                        }
                    }
                    item.write_to(out, indent, depth + 1);
                }
                if let Some(sp) = indent {
                    out.push('\n');
                    for _ in 0..depth * sp {
                        out.push(' ');
                    }
                }
                out.push(']');
            }
            Json::Obj(map) => {
                if map.is_empty() {
                    out.push_str("{}");
                    return;
                }
                out.push('{');
                for (i, (k, v)) in map.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    if let Some(sp) = indent {
                        out.push('\n');
                        for _ in 0..(depth + 1) * sp {
                            out.push(' ');
                        }
                    }
                    write_escaped_str(out, k);
                    out.push_str(if indent.is_some() { ": " } else { ":" });
                    v.write_to(out, indent, depth + 1);
                }
                if let Some(sp) = indent {
                    out.push('\n');
                    for _ in 0..depth * sp {
                        out.push(' ');
                    }
                }
                out.push('}');
            }
        }
    }
}

pub fn obj(pairs: Vec<(&str, Json)>) -> Json {
    let mut map = BTreeMap::new();
    for (k, v) in pairs {
        map.insert(k.to_string(), v);
    }
    Json::Obj(map)
}

fn write_escaped_str(out: &mut String, s: &str) {
    out.push('"');
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\x08' => out.push_str("\\b"),
            '\x0c' => out.push_str("\\f"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out.push('"');
}

struct Parser<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.pos).copied()
    }

    fn next(&mut self) -> Option<u8> {
        let b = self.peek()?;
        self.pos += 1;
        Some(b)
    }

    fn skip_ws(&mut self) {
        while let Some(b) = self.peek() {
            if matches!(b, b' ' | b'\t' | b'\r' | b'\n') {
                self.pos += 1;
            } else {
                break;
            }
        }
    }

    fn parse_value(&mut self, depth: usize) -> Result<Json, String> {
        if depth > 64 {
            return Err("JSON nesting too deep".into());
        }
        self.skip_ws();
        match self.peek() {
            Some(b'n') => self.expect_lit(b"null", Json::Null),
            Some(b't') => self.expect_lit(b"true", Json::Bool(true)),
            Some(b'f') => self.expect_lit(b"false", Json::Bool(false)),
            Some(b'"') => self.parse_str().map(Json::Str),
            Some(b'[') => self.parse_arr(depth),
            Some(b'{') => self.parse_obj(depth),
            Some(b'-' | b'0'..=b'9') => self.parse_num(),
            _ => Err(format!("Unexpected token at offset {}", self.pos)),
        }
    }

    fn expect_lit(&mut self, lit: &[u8], val: Json) -> Result<Json, String> {
        if self.bytes[self.pos..].starts_with(lit) {
            self.pos += lit.len();
            Ok(val)
        } else {
            Err("Invalid literal in JSON".into())
        }
    }

    fn parse_str(&mut self) -> Result<String, String> {
        if self.next() != Some(b'"') {
            return Err("Expected '\"'".into());
        }
        let mut out = String::new();
        while let Some(b) = self.next() {
            match b {
                b'"' => return Ok(out),
                b'\\' => match self.next() {
                    Some(b'"') => out.push('"'),
                    Some(b'\\') => out.push('\\'),
                    Some(b'/') => out.push('/'),
                    Some(b'b') => out.push('\x08'),
                    Some(b'f') => out.push('\x0c'),
                    Some(b'n') => out.push('\n'),
                    Some(b'r') => out.push('\r'),
                    Some(b't') => out.push('\t'),
                    Some(b'u') => {
                        let code = self.parse_hex4()?;
                        if (0xD800..=0xDBFF).contains(&code) {
                            if self.next() == Some(b'\\') && self.next() == Some(b'u') {
                                let lo = self.parse_hex4()?;
                                if (0xDC00..=0xDFFF).contains(&lo) {
                                    let cp = 0x10000
                                        + (((code as u32 - 0xD800) << 10) | (lo as u32 - 0xDC00));
                                    if let Some(c) = char::from_u32(cp) {
                                        out.push(c);
                                    }
                                    continue;
                                }
                            }
                            return Err("Invalid UTF-16 surrogate in JSON".into());
                        }
                        if let Some(c) = char::from_u32(code as u32) {
                            out.push(c);
                        }
                    }
                    _ => return Err("Invalid escape in JSON string".into()),
                },
                0x00..=0x1F => return Err("Unescaped control byte in JSON string".into()),
                first if first < 0x80 => out.push(first as char),
                first => {
                    let start = self.pos - 1;
                    let width = if first & 0xE0 == 0xC0 {
                        2
                    } else if first & 0xF0 == 0xE0 {
                        3
                    } else if first & 0xF8 == 0xF0 {
                        4
                    } else {
                        return Err("Invalid UTF-8 in JSON".into());
                    };
                    let end = start + width;
                    if end > self.bytes.len() {
                        return Err("Truncated UTF-8 in JSON".into());
                    }
                    let slice = std::str::from_utf8(&self.bytes[start..end])
                        .map_err(|_| "Invalid UTF-8 sequence")?;
                    out.push_str(slice);
                    self.pos = end;
                }
            }
        }
        Err("Unterminated JSON string".into())
    }

    fn parse_hex4(&mut self) -> Result<u16, String> {
        let mut v: u16 = 0;
        for _ in 0..4 {
            let d = match self.next() {
                Some(c @ b'0'..=b'9') => (c - b'0') as u16,
                Some(c @ b'a'..=b'f') => (c - b'a' + 10) as u16,
                Some(c @ b'A'..=b'F') => (c - b'A' + 10) as u16,
                _ => return Err("Invalid \\u escape".into()),
            };
            v = (v << 4) | d;
        }
        Ok(v)
    }

    fn parse_num(&mut self) -> Result<Json, String> {
        let start = self.pos;
        if self.peek() == Some(b'-') {
            self.pos += 1;
        }
        while let Some(b'0'..=b'9') = self.peek() {
            self.pos += 1;
        }
        if self.peek() == Some(b'.') {
            self.pos += 1;
            while let Some(b'0'..=b'9') = self.peek() {
                self.pos += 1;
            }
        }
        if matches!(self.peek(), Some(b'e' | b'E')) {
            self.pos += 1;
            if matches!(self.peek(), Some(b'+' | b'-')) {
                self.pos += 1;
            }
            while let Some(b'0'..=b'9') = self.peek() {
                self.pos += 1;
            }
        }
        let s = std::str::from_utf8(&self.bytes[start..self.pos])
            .map_err(|_| "Invalid number")?;
        let n: f64 = s.parse().map_err(|_| format!("Invalid number: {s}"))?;
        if !n.is_finite() {
            return Err("Non-finite JSON number".into());
        }
        Ok(Json::Num(n))
    }

    fn parse_arr(&mut self, depth: usize) -> Result<Json, String> {
        self.next();
        self.skip_ws();
        let mut items = Vec::new();
        if self.peek() == Some(b']') {
            self.next();
            return Ok(Json::Arr(items));
        }
        loop {
            items.push(self.parse_value(depth + 1)?);
            self.skip_ws();
            match self.next() {
                Some(b',') => {}
                Some(b']') => return Ok(Json::Arr(items)),
                _ => return Err("Expected ',' or ']' in JSON array".into()),
            }
        }
    }

    fn parse_obj(&mut self, depth: usize) -> Result<Json, String> {
        self.next();
        self.skip_ws();
        let mut map = BTreeMap::new();
        if self.peek() == Some(b'}') {
            self.next();
            return Ok(Json::Obj(map));
        }
        loop {
            self.skip_ws();
            let k = self.parse_str()?;
            self.skip_ws();
            if self.next() != Some(b':') {
                return Err("Expected ':' after JSON object key".into());
            }
            let v = self.parse_value(depth + 1)?;
            map.insert(k, v);
            self.skip_ws();
            match self.next() {
                Some(b',') => {}
                Some(b'}') => return Ok(Json::Obj(map)),
                _ => return Err("Expected ',' or '}' in JSON object".into()),
            }
        }
    }
}
