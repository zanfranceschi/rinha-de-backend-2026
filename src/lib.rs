pub mod search;
pub mod simd;

use std::{collections::HashMap, fs, path::Path};

use anyhow::{Context, Result, anyhow};
use serde::{Deserialize, Serialize};

pub const DIMENSIONS: usize = 14;
pub const PACKED_DIMENSIONS: usize = 16;
pub const TOP_K: usize = 5;
pub const ARTIFACT_VERSION: u32 = 2;

#[derive(Debug, Clone, Deserialize)]
pub struct FraudRequest<'a> {
    #[serde(borrow)]
    pub id: &'a str,
    #[serde(borrow)]
    pub transaction: Transaction<'a>,
    #[serde(borrow)]
    pub customer: Customer<'a>,
    #[serde(borrow)]
    pub merchant: Merchant<'a>,
    pub terminal: Terminal,
    #[serde(borrow)]
    pub last_transaction: Option<LastTransaction<'a>>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Transaction<'a> {
    pub amount: f32,
    pub installments: u32,
    #[serde(borrow)]
    pub requested_at: &'a str,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Customer<'a> {
    pub avg_amount: f32,
    pub tx_count_24h: u32,
    #[serde(borrow)]
    pub known_merchants: Vec<&'a str>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Merchant<'a> {
    #[serde(borrow)]
    pub id: &'a str,
    #[serde(borrow)]
    pub mcc: &'a str,
    pub avg_amount: f32,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Terminal {
    pub is_online: bool,
    pub card_present: bool,
    pub km_from_home: f32,
}

#[derive(Debug, Clone, Deserialize)]
pub struct LastTransaction<'a> {
    #[serde(borrow)]
    pub timestamp: &'a str,
    pub km_from_current: f32,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct FraudResponse {
    pub approved: bool,
    pub fraud_score: f32,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Normalization {
    pub max_amount: f32,
    pub max_installments: f32,
    pub amount_vs_avg_ratio: f32,
    pub max_minutes: f32,
    pub max_km: f32,
    pub max_tx_count_24h: f32,
    pub max_merchant_avg_amount: f32,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ReferenceRecord {
    pub vector: [f32; DIMENSIONS],
    pub label: String,
}

pub type MccRiskMap = HashMap<String, f32>;

pub fn load_normalization(path: impl AsRef<Path>) -> Result<Normalization> {
    let raw = fs::read_to_string(path.as_ref())
        .with_context(|| format!("failed to read {}", path.as_ref().display()))?;
    serde_json::from_str(&raw).context("failed to parse normalization json")
}

pub fn load_mcc_risk(path: impl AsRef<Path>) -> Result<MccRiskMap> {
    let raw = fs::read_to_string(path.as_ref())
        .with_context(|| format!("failed to read {}", path.as_ref().display()))?;
    serde_json::from_str(&raw).context("failed to parse mcc risk json")
}

pub fn vectorize(
    request: &FraudRequest<'_>,
    normalization: &Normalization,
    mcc_risk: &MccRiskMap,
) -> Result<[f32; DIMENSIONS]> {
    let requested_at = ParsedTimestamp::parse(&request.transaction.requested_at)
        .with_context(|| format!("invalid requested_at {}", request.transaction.requested_at))?;

    let amount_vs_avg = if request.customer.avg_amount <= 0.0 {
        1.0
    } else {
        (request.transaction.amount / request.customer.avg_amount)
            / normalization.amount_vs_avg_ratio
    };

    let mut vector = [0.0; DIMENSIONS];
    vector[0] = clamp01(request.transaction.amount / normalization.max_amount);
    vector[1] = clamp01(request.transaction.installments as f32 / normalization.max_installments);
    vector[2] = clamp01(amount_vs_avg);
    vector[3] = requested_at.hour as f32 / 23.0;
    vector[4] = requested_at.weekday_from_monday as f32 / 6.0;

    match &request.last_transaction {
        Some(last_tx) => {
            let last_ts = ParsedTimestamp::parse(&last_tx.timestamp).with_context(|| {
                format!("invalid last_transaction.timestamp {}", last_tx.timestamp)
            })?;
            let minutes = (requested_at.epoch_minutes - last_ts.epoch_minutes).max(0) as f32;
            vector[5] = clamp01(minutes / normalization.max_minutes);
            vector[6] = clamp01(last_tx.km_from_current / normalization.max_km);
        }
        None => {
            vector[5] = -1.0;
            vector[6] = -1.0;
        }
    }

    vector[7] = clamp01(request.terminal.km_from_home / normalization.max_km);
    vector[8] = clamp01(request.customer.tx_count_24h as f32 / normalization.max_tx_count_24h);
    vector[9] = if request.terminal.is_online { 1.0 } else { 0.0 };
    vector[10] = if request.terminal.card_present {
        1.0
    } else {
        0.0
    };
    vector[11] = if request
        .customer
        .known_merchants
        .iter()
        .any(|known| *known == request.merchant.id)
    {
        0.0
    } else {
        1.0
    };
    vector[12] = *mcc_risk.get(request.merchant.mcc).unwrap_or(&0.5);
    vector[13] = clamp01(request.merchant.avg_amount / normalization.max_merchant_avg_amount);

    Ok(vector)
}

pub fn quantize_vector(vector: &[f32; DIMENSIONS]) -> [i8; DIMENSIONS] {
    let mut out = [0_i8; DIMENSIONS];
    for (idx, value) in vector.iter().enumerate() {
        let scaled = value.clamp(-1.0, 1.0) * 127.0;
        out[idx] = scaled.round() as i8;
    }
    out
}

pub fn quantize_vector_padded(vector: &[f32; DIMENSIONS]) -> [i8; PACKED_DIMENSIONS] {
    let quantized = quantize_vector(vector);
    let mut out = [0_i8; PACKED_DIMENSIONS];
    out[..DIMENSIONS].copy_from_slice(&quantized);
    out
}

pub fn pad_centroid(centroid: &[f32; DIMENSIONS]) -> [f32; PACKED_DIMENSIONS] {
    let mut out = [0.0_f32; PACKED_DIMENSIONS];
    out[..DIMENSIONS].copy_from_slice(centroid);
    out
}

pub fn dequantize_component(value: i8) -> f32 {
    value as f32 / 127.0
}

pub fn squared_distance_i8_scalar(query: &[i8; PACKED_DIMENSIONS], candidate: &[u8]) -> u32 {
    let mut sum = 0_u32;
    for idx in 0..PACKED_DIMENSIONS {
        let delta = query[idx] as i32 - candidate[idx] as i8 as i32;
        sum += (delta * delta) as u32;
    }
    sum
}

pub fn squared_distance_f32_scalar(
    query: &[f32; PACKED_DIMENSIONS],
    candidate: &[f32; PACKED_DIMENSIONS],
) -> f32 {
    let mut sum = 0.0_f32;
    for idx in 0..PACKED_DIMENSIONS {
        let delta = query[idx] - candidate[idx];
        sum += delta * delta;
    }
    sum
}

pub fn score_neighbors(labels: &[u8]) -> FraudResponse {
    let fraud_count = labels.iter().filter(|label| **label == 1).count();
    let fraud_score = fraud_count as f32 / TOP_K as f32;
    FraudResponse {
        approved: fraud_score < 0.6,
        fraud_score,
    }
}

pub fn deny_response() -> FraudResponse {
    FraudResponse {
        approved: false,
        fraud_score: 1.0,
    }
}

pub fn heuristic_response(
    request: &FraudRequest<'_>,
    normalization: &Normalization,
    mcc_risk: &MccRiskMap,
) -> FraudResponse {
    let mut score = 0.0_f32;
    let avg_amount = if request.customer.avg_amount <= 0.0 {
        normalization.max_amount
    } else {
        request.customer.avg_amount
    };

    if request.transaction.amount > avg_amount * 4.0 {
        score += 0.25;
    }
    if !request
        .customer
        .known_merchants
        .iter()
        .any(|known| *known == request.merchant.id)
    {
        score += 0.2;
    }
    if request.terminal.is_online {
        score += 0.15;
    }
    if !request.terminal.card_present {
        score += 0.15;
    }
    if request.terminal.km_from_home > normalization.max_km * 0.4 {
        score += 0.15;
    }
    score += mcc_risk.get(request.merchant.mcc).copied().unwrap_or(0.5) * 0.1;

    let fraud_score = score.clamp(0.0, 1.0);
    FraudResponse {
        approved: fraud_score < 0.6,
        fraud_score: ((fraud_score * TOP_K as f32).round() / TOP_K as f32).clamp(0.0, 1.0),
    }
}

pub fn clamp01(value: f32) -> f32 {
    value.clamp(0.0, 1.0)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct ParsedTimestamp {
    hour: u32,
    weekday_from_monday: u32,
    epoch_minutes: i64,
}

impl ParsedTimestamp {
    fn parse(raw: &str) -> Result<Self> {
        let bytes = raw.as_bytes();
        if bytes.len() != 20
            || bytes[4] != b'-'
            || bytes[7] != b'-'
            || bytes[10] != b'T'
            || bytes[13] != b':'
            || bytes[16] != b':'
            || bytes[19] != b'Z'
        {
            return Err(anyhow!("expected UTC timestamp YYYY-MM-DDTHH:MM:SSZ"));
        }

        let year = parse_digits(bytes, 0, 4)? as i32;
        let month = parse_digits(bytes, 5, 2)? as u32;
        let day = parse_digits(bytes, 8, 2)? as u32;
        let hour = parse_digits(bytes, 11, 2)? as u32;
        let minute = parse_digits(bytes, 14, 2)? as u32;
        let second = parse_digits(bytes, 17, 2)? as u32;

        if !(1..=12).contains(&month)
            || day == 0
            || day > days_in_month(year, month)
            || hour > 23
            || minute > 59
            || second > 59
        {
            return Err(anyhow!("timestamp component out of range"));
        }

        let days = days_from_civil(year, month, day);
        Ok(Self {
            hour,
            weekday_from_monday: weekday_from_days(days),
            epoch_minutes: days * 1_440 + (hour as i64) * 60 + minute as i64,
        })
    }
}

fn parse_digits(bytes: &[u8], start: usize, len: usize) -> Result<u32> {
    let mut value = 0_u32;
    for byte in &bytes[start..start + len] {
        if !byte.is_ascii_digit() {
            return Err(anyhow!("non-digit timestamp component"));
        }
        value = value * 10 + (byte - b'0') as u32;
    }
    Ok(value)
}

fn days_in_month(year: i32, month: u32) -> u32 {
    match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 if is_leap_year(year) => 29,
        2 => 28,
        _ => 0,
    }
}

fn is_leap_year(year: i32) -> bool {
    (year % 4 == 0 && year % 100 != 0) || year % 400 == 0
}

fn days_from_civil(year: i32, month: u32, day: u32) -> i64 {
    let year = year - i32::from(month <= 2);
    let era = if year >= 0 { year } else { year - 399 } / 400;
    let yoe = year - era * 400;
    let month = month as i32;
    let day = day as i32;
    let doy = (153 * (month + if month > 2 { -3 } else { 9 }) + 2) / 5 + day - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    (era * 146_097 + doe - 719_468) as i64
}

fn weekday_from_days(days_since_epoch: i64) -> u32 {
    (days_since_epoch + 3).rem_euclid(7) as u32
}

pub fn validate_reference_record(record: &ReferenceRecord) -> Result<()> {
    if record.label != "fraud" && record.label != "legit" {
        return Err(anyhow!("unexpected label {}", record.label));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn approx_eq(left: f32, right: f32) {
        assert!((left - right).abs() < 0.001, "left={left} right={right}");
    }

    fn test_normalization() -> Normalization {
        Normalization {
            max_amount: 10000.0,
            max_installments: 12.0,
            amount_vs_avg_ratio: 10.0,
            max_minutes: 1440.0,
            max_km: 1000.0,
            max_tx_count_24h: 20.0,
            max_merchant_avg_amount: 10000.0,
        }
    }

    fn test_mcc() -> MccRiskMap {
        HashMap::from([("5411".to_string(), 0.15), ("7802".to_string(), 0.75)])
    }

    #[test]
    fn vectorizes_legit_example() {
        let request: FraudRequest = serde_json::from_str(
            r#"{
                "id": "tx-1329056812",
                "transaction": { "amount": 41.12, "installments": 2, "requested_at": "2026-03-11T18:45:53Z" },
                "customer": { "avg_amount": 82.24, "tx_count_24h": 3, "known_merchants": ["MERC-003", "MERC-016"] },
                "merchant": { "id": "MERC-016", "mcc": "5411", "avg_amount": 60.25 },
                "terminal": { "is_online": false, "card_present": true, "km_from_home": 29.23 },
                "last_transaction": null
            }"#,
        )
        .unwrap();

        let vector = vectorize(&request, &test_normalization(), &test_mcc()).unwrap();

        let expected = [
            0.0041, 0.1667, 0.05, 0.7826, 0.3333, -1.0, -1.0, 0.0292, 0.15, 0.0, 1.0, 0.0, 0.15,
            0.006,
        ];
        for (left, right) in vector.iter().zip(expected.iter()) {
            approx_eq(*left, *right);
        }
    }

    #[test]
    fn vectorizes_fraud_example() {
        let request: FraudRequest = serde_json::from_str(
            r#"{
                "id": "tx-3330991687",
                "transaction": { "amount": 9505.97, "installments": 10, "requested_at": "2026-03-14T05:15:12Z" },
                "customer": { "avg_amount": 81.28, "tx_count_24h": 20, "known_merchants": ["MERC-008", "MERC-007", "MERC-005"] },
                "merchant": { "id": "MERC-068", "mcc": "7802", "avg_amount": 54.86 },
                "terminal": { "is_online": false, "card_present": true, "km_from_home": 952.27 },
                "last_transaction": null
            }"#,
        )
        .unwrap();

        let vector = vectorize(&request, &test_normalization(), &test_mcc()).unwrap();

        let expected = [
            0.9506, 0.8333, 1.0, 0.2174, 0.8333, -1.0, -1.0, 0.9523, 1.0, 0.0, 1.0, 1.0, 0.75,
            0.0055,
        ];
        for (left, right) in vector.iter().zip(expected.iter()) {
            approx_eq(*left, *right);
        }
    }

    #[test]
    fn timestamp_parser_extracts_weekday_and_minutes() {
        let timestamp = ParsedTimestamp::parse("2026-03-11T18:45:53Z").unwrap();
        assert_eq!(timestamp.hour, 18);
        assert_eq!(timestamp.weekday_from_monday, 2);

        let epoch = ParsedTimestamp::parse("1970-01-01T00:00:00Z").unwrap();
        assert_eq!(epoch.weekday_from_monday, 3);
        assert_eq!(epoch.epoch_minutes, 0);

        let leap_day = ParsedTimestamp::parse("2024-02-29T23:59:59Z").unwrap();
        assert_eq!(leap_day.weekday_from_monday, 3);
    }

    #[test]
    fn timestamp_parser_rejects_malformed_or_out_of_range_values() {
        assert!(ParsedTimestamp::parse("2026-03-11T18:45:53+00:00").is_err());
        assert!(ParsedTimestamp::parse("2026-02-29T18:45:53Z").is_err());
        assert!(ParsedTimestamp::parse("2026-03-11T24:00:00Z").is_err());
        assert!(ParsedTimestamp::parse("2026-03-11T18:60:00Z").is_err());
        assert!(ParsedTimestamp::parse("2026-03-11T18:45:60Z").is_err());
    }

    #[test]
    fn vectorize_clamps_negative_elapsed_minutes() {
        let request: FraudRequest = serde_json::from_str(
            r#"{
                "id": "tx-future-last",
                "transaction": { "amount": 100.0, "installments": 1, "requested_at": "2026-03-11T18:45:53Z" },
                "customer": { "avg_amount": 100.0, "tx_count_24h": 1, "known_merchants": ["MERC-016"] },
                "merchant": { "id": "MERC-016", "mcc": "5411", "avg_amount": 60.25 },
                "terminal": { "is_online": false, "card_present": true, "km_from_home": 10.0 },
                "last_transaction": { "timestamp": "2026-03-11T18:46:53Z", "km_from_current": 15.0 }
            }"#,
        )
        .unwrap();

        let vector = vectorize(&request, &test_normalization(), &test_mcc()).unwrap();
        approx_eq(vector[5], 0.0);
        approx_eq(vector[6], 0.015);
    }

    #[test]
    fn vectorize_rejects_malformed_timestamp() {
        let request: FraudRequest = serde_json::from_str(
            r#"{
                "id": "tx-bad-time",
                "transaction": { "amount": 100.0, "installments": 1, "requested_at": "2026-03-11 18:45:53" },
                "customer": { "avg_amount": 100.0, "tx_count_24h": 1, "known_merchants": ["MERC-016"] },
                "merchant": { "id": "MERC-016", "mcc": "5411", "avg_amount": 60.25 },
                "terminal": { "is_online": false, "card_present": true, "km_from_home": 10.0 },
                "last_transaction": null
            }"#,
        )
        .unwrap();

        assert!(vectorize(&request, &test_normalization(), &test_mcc()).is_err());
    }

    #[test]
    fn request_deserialization_uses_borrowed_hot_path_strings() {
        let raw = br#"{
            "id": "tx-borrowed",
            "transaction": { "amount": 41.12, "installments": 2, "requested_at": "2026-03-11T18:45:53Z" },
            "customer": { "avg_amount": 82.24, "tx_count_24h": 3, "known_merchants": ["MERC-003", "MERC-016"] },
            "merchant": { "id": "MERC-016", "mcc": "5411", "avg_amount": 60.25 },
            "terminal": { "is_online": false, "card_present": true, "km_from_home": 29.23 },
            "last_transaction": null
        }"#;
        let request: FraudRequest<'_> = serde_json::from_slice(raw).unwrap();

        assert_eq!(request.id, "tx-borrowed");
        assert_eq!(request.transaction.requested_at, "2026-03-11T18:45:53Z");
        assert_eq!(request.customer.known_merchants[0], "MERC-003");
        assert_eq!(request.merchant.id, "MERC-016");
    }

    #[test]
    fn quantization_preserves_sentinel() {
        let vector = [
            0.0, 1.0, 0.5, 0.25, 0.75, -1.0, -1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.5, 0.25,
        ];
        let quantized = quantize_vector(&vector);
        assert_eq!(quantized[5], -127);
        assert_eq!(quantized[6], -127);
    }

    #[test]
    fn padded_quantization_zeroes_extra_lanes() {
        let vector = [
            0.0, 1.0, 0.5, 0.25, 0.75, -1.0, -1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.5, 0.25,
        ];
        let quantized = quantize_vector_padded(&vector);
        assert_eq!(quantized[5], -127);
        assert_eq!(quantized[6], -127);
        assert_eq!(quantized[14], 0);
        assert_eq!(quantized[15], 0);
    }

    #[test]
    fn scores_top_five_neighbors() {
        let response = score_neighbors(&[1, 1, 1, 0, 0]);
        assert_eq!(
            response,
            FraudResponse {
                approved: false,
                fraud_score: 0.6
            }
        );
    }
}
