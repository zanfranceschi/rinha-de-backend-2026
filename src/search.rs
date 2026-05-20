use std::{
    cmp::Ordering,
    fs::File,
    io::Read,
    path::{Path, PathBuf},
};

use anyhow::{Context, Result, anyhow};
use memmap2::Mmap;
use serde::{Deserialize, Serialize};

use crate::{
    ARTIFACT_VERSION, DIMENSIONS, FraudResponse, PACKED_DIMENSIONS, TOP_K, dequantize_component,
    pad_centroid, quantize_vector_padded, score_neighbors, simd::DistanceKernels, simd::KernelMode,
    simd::select_distance_kernels,
};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ArtifactMeta {
    pub version: u32,
    pub dimensions: usize,
    pub packed_dimensions: usize,
    pub vector_count: u64,
    pub cluster_count: usize,
    pub probe_count: usize,
    pub list_offsets: Vec<u64>,
    pub list_lengths: Vec<u32>,
}

pub struct SearchEngine {
    pub meta: ArtifactMeta,
    centroids: Vec<[f32; PACKED_DIMENSIONS]>,
    vectors: Mmap,
    labels: Mmap,
    kernels: DistanceKernels,
    _artifact_dir: PathBuf,
}

#[derive(Clone, Copy, Debug)]
struct Neighbor {
    distance: u32,
    label: u8,
}

struct TopK {
    entries: [Neighbor; TOP_K],
    len: usize,
}

impl TopK {
    fn new() -> Self {
        Self {
            entries: [Neighbor {
                distance: u32::MAX,
                label: 0,
            }; TOP_K],
            len: 0,
        }
    }

    fn insert(&mut self, distance: u32, label: u8) {
        if self.len < TOP_K {
            self.entries[self.len] = Neighbor { distance, label };
            self.len += 1;
            return;
        }

        let mut worst_idx = 0;
        for idx in 1..TOP_K {
            if self.entries[idx].distance > self.entries[worst_idx].distance {
                worst_idx = idx;
            }
        }

        if distance < self.entries[worst_idx].distance {
            self.entries[worst_idx] = Neighbor { distance, label };
        }
    }

    fn labels(&self) -> [u8; TOP_K] {
        let mut labels = [0_u8; TOP_K];
        for (dst, entry) in labels.iter_mut().zip(self.entries.iter()) {
            *dst = entry.label;
        }
        labels
    }
}

impl SearchEngine {
    pub fn open(path: impl AsRef<Path>, probe_override: Option<usize>) -> Result<Self> {
        let dir = path.as_ref().to_path_buf();
        let meta_raw = std::fs::read_to_string(dir.join("meta.json"))
            .with_context(|| format!("failed to read {}", dir.join("meta.json").display()))?;
        let mut meta: ArtifactMeta =
            serde_json::from_str(&meta_raw).context("invalid meta.json")?;
        if let Some(probe_count) = probe_override {
            meta.probe_count = probe_count.max(1).min(meta.cluster_count);
        }

        validate_meta(&meta)?;

        let centroids = load_centroids(&dir.join("centroids.bin"), meta.cluster_count)?;
        let vectors_file = File::open(dir.join("vectors.bin"))
            .with_context(|| format!("failed to open {}", dir.join("vectors.bin").display()))?;
        let labels_file = File::open(dir.join("labels.bin"))
            .with_context(|| format!("failed to open {}", dir.join("labels.bin").display()))?;

        let vectors = unsafe { Mmap::map(&vectors_file) }.context("failed to mmap vectors.bin")?;
        let labels = unsafe { Mmap::map(&labels_file) }.context("failed to mmap labels.bin")?;

        validate_artifact_sizes(&meta, &vectors, &labels)?;

        Ok(Self {
            meta,
            centroids,
            vectors,
            labels,
            kernels: select_distance_kernels(),
            _artifact_dir: dir,
        })
    }

    pub fn score(&self, query: &[f32; DIMENSIONS]) -> Result<FraudResponse> {
        let quantized = quantize_vector_padded(query);
        let padded_query = pad_centroid(query);
        let mut ranked_clusters: Vec<(f32, usize)> = self
            .centroids
            .iter()
            .enumerate()
            .map(|(idx, centroid)| {
                (
                    (self.kernels.centroid_distance)(&padded_query, centroid),
                    idx,
                )
            })
            .collect();
        ranked_clusters
            .sort_by(|left, right| left.0.partial_cmp(&right.0).unwrap_or(Ordering::Equal));

        let mut top_k = TopK::new();
        let mut scanned = 0_usize;
        for (_, cluster_idx) in ranked_clusters {
            self.scan_cluster(cluster_idx, &quantized, &mut top_k)?;
            scanned += 1;
            if scanned >= self.meta.probe_count && top_k.len >= TOP_K {
                break;
            }
        }

        if top_k.len < TOP_K {
            return Err(anyhow!("not enough candidates to score top-5 neighbors"));
        }

        Ok(score_neighbors(&top_k.labels()))
    }

    pub fn avx2_enabled(&self) -> bool {
        self.kernels.avx2_enabled
    }

    pub fn candidate_kernel_mode(&self) -> KernelMode {
        self.kernels.candidate_mode
    }

    pub fn centroid_kernel_mode(&self) -> KernelMode {
        self.kernels.centroid_mode
    }

    fn scan_cluster(
        &self,
        cluster_idx: usize,
        query: &[i8; PACKED_DIMENSIONS],
        top_k: &mut TopK,
    ) -> Result<()> {
        let start = self.meta.list_offsets[cluster_idx] as usize;
        let len = self.meta.list_lengths[cluster_idx] as usize;
        for item_idx in 0..len {
            let absolute_idx = start + item_idx;
            let base = absolute_idx * PACKED_DIMENSIONS;
            let candidate = self
                .vectors
                .get(base..base + PACKED_DIMENSIONS)
                .ok_or_else(|| anyhow!("vector slice out of bounds"))?;
            let label = *self
                .labels
                .get(absolute_idx)
                .ok_or_else(|| anyhow!("label index out of bounds"))?;
            let distance = (self.kernels.candidate_distance)(query, candidate);
            top_k.insert(distance, label);
        }
        Ok(())
    }
}

fn validate_meta(meta: &ArtifactMeta) -> Result<()> {
    if meta.version != ARTIFACT_VERSION {
        return Err(anyhow!(
            "artifact version mismatch: expected {} got {}",
            ARTIFACT_VERSION,
            meta.version
        ));
    }
    if meta.dimensions != DIMENSIONS {
        return Err(anyhow!(
            "artifact dimension mismatch: expected {} got {}",
            DIMENSIONS,
            meta.dimensions
        ));
    }
    if meta.packed_dimensions != PACKED_DIMENSIONS {
        return Err(anyhow!(
            "artifact packed dimension mismatch: expected {} got {}",
            PACKED_DIMENSIONS,
            meta.packed_dimensions
        ));
    }
    Ok(())
}

fn validate_artifact_sizes(meta: &ArtifactMeta, vectors: &Mmap, labels: &Mmap) -> Result<()> {
    let expected_vector_bytes = meta.vector_count as usize * PACKED_DIMENSIONS;
    if vectors.len() != expected_vector_bytes {
        return Err(anyhow!(
            "vectors.bin size mismatch: expected {} got {}",
            expected_vector_bytes,
            vectors.len()
        ));
    }
    if labels.len() != meta.vector_count as usize {
        return Err(anyhow!(
            "labels.bin size mismatch: expected {} got {}",
            meta.vector_count,
            labels.len()
        ));
    }
    Ok(())
}

fn load_centroids(path: &Path, cluster_count: usize) -> Result<Vec<[f32; PACKED_DIMENSIONS]>> {
    let mut file =
        File::open(path).with_context(|| format!("failed to open {}", path.display()))?;
    let mut raw = Vec::new();
    file.read_to_end(&mut raw)
        .with_context(|| format!("failed to read {}", path.display()))?;

    let expected_len = cluster_count * PACKED_DIMENSIONS * std::mem::size_of::<f32>();
    if raw.len() != expected_len {
        return Err(anyhow!(
            "centroids.bin size mismatch: expected {expected_len} got {}",
            raw.len()
        ));
    }

    let mut centroids = Vec::with_capacity(cluster_count);
    for chunk in raw.chunks_exact(PACKED_DIMENSIONS * 4) {
        let mut centroid = [0.0_f32; PACKED_DIMENSIONS];
        for (idx, bytes) in chunk.chunks_exact(4).enumerate() {
            centroid[idx] = f32::from_le_bytes(bytes.try_into().unwrap());
        }
        centroids.push(centroid);
    }
    Ok(centroids)
}

pub fn centroid_from_quantized(chunk: &[i8]) -> [f32; PACKED_DIMENSIONS] {
    let mut centroid = [0.0_f32; PACKED_DIMENSIONS];
    for idx in 0..PACKED_DIMENSIONS {
        centroid[idx] = dequantize_component(chunk[idx]);
    }
    centroid
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn top_k_prefers_smallest_distance() {
        let mut top_k = TopK::new();
        top_k.insert(80, 0);
        top_k.insert(50, 1);
        top_k.insert(40, 1);
        top_k.insert(30, 0);
        top_k.insert(20, 1);
        top_k.insert(10, 1);

        let labels = top_k.labels();
        assert_eq!(labels.iter().filter(|label| **label == 1).count(), 4);
    }

    #[test]
    fn rejects_incompatible_meta() {
        let meta = ArtifactMeta {
            version: ARTIFACT_VERSION,
            dimensions: DIMENSIONS,
            packed_dimensions: 14,
            vector_count: 1,
            cluster_count: 1,
            probe_count: 1,
            list_offsets: vec![0],
            list_lengths: vec![1],
        };
        assert!(validate_meta(&meta).is_err());
    }
}
