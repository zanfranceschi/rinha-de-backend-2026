use crate::{PACKED_DIMENSIONS, squared_distance_f32_scalar, squared_distance_i8_scalar};

pub type CandidateDistanceFn = fn(&[i8; PACKED_DIMENSIONS], &[u8]) -> u32;
pub type CentroidDistanceFn = fn(&[f32; PACKED_DIMENSIONS], &[f32; PACKED_DIMENSIONS]) -> f32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KernelMode {
    Scalar,
    Avx2,
}

#[derive(Clone, Copy)]
pub struct DistanceKernels {
    pub candidate_distance: CandidateDistanceFn,
    pub centroid_distance: CentroidDistanceFn,
    pub avx2_enabled: bool,
    pub candidate_mode: KernelMode,
    pub centroid_mode: KernelMode,
}

pub fn select_distance_kernels() -> DistanceKernels {
    #[cfg(target_arch = "x86_64")]
    {
        if std::arch::is_x86_feature_detected!("avx2") {
            return DistanceKernels {
                candidate_distance: candidate_distance_avx2_entry,
                centroid_distance: centroid_distance_avx2_entry,
                avx2_enabled: true,
                candidate_mode: KernelMode::Avx2,
                centroid_mode: KernelMode::Avx2,
            };
        }
    }

    DistanceKernels {
        candidate_distance: squared_distance_i8_scalar,
        centroid_distance: squared_distance_f32_scalar,
        avx2_enabled: false,
        candidate_mode: KernelMode::Scalar,
        centroid_mode: KernelMode::Scalar,
    }
}

pub fn target_arch() -> &'static str {
    #[cfg(target_arch = "x86_64")]
    {
        "x86_64"
    }
    #[cfg(target_arch = "aarch64")]
    {
        "aarch64"
    }
    #[cfg(target_arch = "arm")]
    {
        "arm"
    }
    #[cfg(not(any(target_arch = "x86_64", target_arch = "aarch64", target_arch = "arm")))]
    {
        "other"
    }
}

#[cfg(target_arch = "x86_64")]
#[allow(dead_code)]
fn candidate_distance_avx2_entry(query: &[i8; PACKED_DIMENSIONS], candidate: &[u8]) -> u32 {
    unsafe { candidate_distance_avx2(query, candidate) }
}

#[cfg(not(target_arch = "x86_64"))]
#[allow(dead_code)]
fn candidate_distance_avx2_entry(query: &[i8; PACKED_DIMENSIONS], candidate: &[u8]) -> u32 {
    squared_distance_i8_scalar(query, candidate)
}

#[cfg(target_arch = "x86_64")]
#[allow(dead_code)]
fn centroid_distance_avx2_entry(
    query: &[f32; PACKED_DIMENSIONS],
    candidate: &[f32; PACKED_DIMENSIONS],
) -> f32 {
    unsafe { centroid_distance_avx2(query, candidate) }
}

#[cfg(not(target_arch = "x86_64"))]
#[allow(dead_code)]
fn centroid_distance_avx2_entry(
    query: &[f32; PACKED_DIMENSIONS],
    candidate: &[f32; PACKED_DIMENSIONS],
) -> f32 {
    squared_distance_f32_scalar(query, candidate)
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn candidate_distance_avx2(query: &[i8; PACKED_DIMENSIONS], candidate: &[u8]) -> u32 {
    use std::arch::x86_64::*;

    let q = _mm_loadu_si128(query.as_ptr().cast());
    let c = _mm_loadu_si128(candidate.as_ptr().cast());
    let q16 = _mm256_cvtepi8_epi16(q);
    let c16 = _mm256_cvtepi8_epi16(c);
    let diff = _mm256_sub_epi16(q16, c16);
    let sums = _mm256_madd_epi16(diff, diff);
    let mut lanes = [0_i32; PACKED_DIMENSIONS / 2];
    _mm256_storeu_si256(lanes.as_mut_ptr().cast(), sums);
    lanes.iter().map(|value| *value as u32).sum()
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn centroid_distance_avx2(
    query: &[f32; PACKED_DIMENSIONS],
    candidate: &[f32; PACKED_DIMENSIONS],
) -> f32 {
    use std::arch::x86_64::*;

    let q0 = _mm256_loadu_ps(query.as_ptr());
    let c0 = _mm256_loadu_ps(candidate.as_ptr());
    let q1 = _mm256_loadu_ps(query.as_ptr().add(8));
    let c1 = _mm256_loadu_ps(candidate.as_ptr().add(8));

    let d0 = _mm256_sub_ps(q0, c0);
    let d1 = _mm256_sub_ps(q1, c1);
    let s0 = _mm256_mul_ps(d0, d0);
    let s1 = _mm256_mul_ps(d1, d1);

    let mut lanes0 = [0.0_f32; 8];
    let mut lanes1 = [0.0_f32; 8];
    _mm256_storeu_ps(lanes0.as_mut_ptr(), s0);
    _mm256_storeu_ps(lanes1.as_mut_ptr(), s1);

    lanes0.iter().sum::<f32>() + lanes1.iter().sum::<f32>()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{pad_centroid, quantize_vector_padded};

    #[test]
    fn scalar_kernels_match_reference() {
        let left = [
            0.0, 1.0, 0.5, 0.25, 0.75, -1.0, -1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.5, 0.25,
        ];
        let right = [
            1.0, 0.25, 0.25, 0.75, 0.0, -1.0, 0.5, 0.2, 0.8, 0.0, 0.0, 1.0, 0.1, 0.2,
        ];
        let left_q = quantize_vector_padded(&left);
        let right_q = quantize_vector_padded(&right);
        let right_bytes: [u8; PACKED_DIMENSIONS] = right_q.map(|value| value as u8);

        assert_eq!(
            squared_distance_i8_scalar(&left_q, &right_bytes),
            (select_distance_kernels().candidate_distance)(&left_q, &right_bytes)
        );

        let left_f = pad_centroid(&left);
        let right_f = pad_centroid(&right);
        assert!(
            (squared_distance_f32_scalar(&left_f, &right_f)
                - (select_distance_kernels().centroid_distance)(&left_f, &right_f))
            .abs()
                < 0.0001
        );
    }

    #[cfg(target_arch = "x86_64")]
    #[test]
    fn avx2_kernels_match_scalar_when_available() {
        if !std::arch::is_x86_feature_detected!("avx2") {
            return;
        }

        let left = [
            0.0, 1.0, 0.5, 0.25, 0.75, -1.0, -1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.5, 0.25,
        ];
        let right = [
            1.0, 0.25, 0.25, 0.75, 0.0, -1.0, 0.5, 0.2, 0.8, 0.0, 0.0, 1.0, 0.1, 0.2,
        ];
        let left_q = quantize_vector_padded(&left);
        let right_q = quantize_vector_padded(&right);
        let right_bytes: [u8; PACKED_DIMENSIONS] = right_q.map(|value| value as u8);

        let scalar_candidate = squared_distance_i8_scalar(&left_q, &right_bytes);
        let avx_candidate = candidate_distance_avx2_entry(&left_q, &right_bytes);
        assert_eq!(scalar_candidate, avx_candidate);

        let left_f = pad_centroid(&left);
        let right_f = pad_centroid(&right);
        let scalar_centroid = squared_distance_f32_scalar(&left_f, &right_f);
        let avx_centroid = centroid_distance_avx2_entry(&left_f, &right_f);
        assert!((scalar_centroid - avx_centroid).abs() < 0.0001);
    }
}
