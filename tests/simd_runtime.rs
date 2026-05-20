use rinha_backend_2026::{
    PACKED_DIMENSIONS, pad_centroid, quantize_vector_padded, simd, squared_distance_f32_scalar,
    squared_distance_i8_scalar,
};

fn require_avx2() -> bool {
    std::env::var("SIMD_REQUIRE_AVX2")
        .ok()
        .as_deref()
        .map(|value| matches!(value, "1" | "true" | "TRUE" | "yes" | "YES"))
        .unwrap_or(false)
}

#[test]
fn selected_kernels_match_scalar_reference() {
    let left = [
        0.0, 1.0, 0.5, 0.25, 0.75, -1.0, -1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.5, 0.25,
    ];
    let right = [
        1.0, 0.25, 0.25, 0.75, 0.0, -1.0, 0.5, 0.2, 0.8, 0.0, 0.0, 1.0, 0.1, 0.2,
    ];
    let kernels = simd::select_distance_kernels();

    if require_avx2() {
        assert_eq!(
            simd::target_arch(),
            "x86_64",
            "strict AVX2 mode requires x86_64"
        );
        assert!(
            kernels.avx2_enabled,
            "strict AVX2 mode requires AVX2 detection"
        );
        assert_eq!(kernels.candidate_mode, simd::KernelMode::Avx2);
        assert_eq!(kernels.centroid_mode, simd::KernelMode::Avx2);
    }

    let left_q = quantize_vector_padded(&left);
    let right_q = quantize_vector_padded(&right);
    let right_bytes: [u8; PACKED_DIMENSIONS] = right_q.map(|value| value as u8);
    assert_eq!(
        squared_distance_i8_scalar(&left_q, &right_bytes),
        (kernels.candidate_distance)(&left_q, &right_bytes)
    );

    let left_f = pad_centroid(&left);
    let right_f = pad_centroid(&right);
    assert!(
        (squared_distance_f32_scalar(&left_f, &right_f)
            - (kernels.centroid_distance)(&left_f, &right_f))
        .abs()
            < 0.0001
    );
}
