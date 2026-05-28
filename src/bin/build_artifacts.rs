use std::{
    env,
    fs::{self, File},
    io::{BufReader, Read, Write},
    path::{Path, PathBuf},
};

use anyhow::{Context, Result, anyhow};
use rinha_backend_2026::search::ArtifactMeta;
use rinha_backend_2026::{
    ARTIFACT_VERSION, DIMENSIONS, PACKED_DIMENSIONS, ReferenceRecord, quantize_vector_padded,
    validate_reference_record,
};
use serde::Deserialize;
use serde::de::{Deserializer, IgnoredAny, SeqAccess, Visitor};

const DEFAULT_INPUT: &str = "resources/references.json.gz";
const DEFAULT_OUTPUT: &str = "docker/artifacts";
const DEFAULT_CLUSTERS: usize = 2048;
const DEFAULT_PROBES: usize = 12;
const SAMPLE_TARGET: usize = 16_384;
const KMEANS_ITERATIONS: usize = 6;

fn main() -> Result<()> {
    let config = Config::from_env()?;
    fs::create_dir_all(&config.output)
        .with_context(|| format!("failed to create {}", config.output.display()))?;

    let references = load_references(&config.input)?;
    let vector_count = references.vectors.len() / PACKED_DIMENSIONS;
    if vector_count == 0 {
        return Err(anyhow!("no vectors found in input dataset"));
    }

    let cluster_count = config.clusters.min(vector_count).max(1);
    let sample_indices = sampled_indices(vector_count, SAMPLE_TARGET.min(vector_count));
    let centroids = fit_centroids(&references.vectors, &sample_indices, cluster_count);
    let assignments = assign_vectors(&references.vectors, &centroids);
    let counts = cluster_counts(&assignments, cluster_count);
    let offsets = prefix_offsets(&counts);
    let (ordered_vectors, ordered_labels) = reorder_by_cluster(
        &references.vectors,
        &references.labels,
        &assignments,
        &offsets,
    );
    let runtime_centroids =
        recompute_runtime_centroids(&ordered_vectors, &counts, &offsets, &centroids);

    write_vectors(&config.output.join("vectors.bin"), &ordered_vectors)?;
    write_labels(&config.output.join("labels.bin"), &ordered_labels)?;
    write_centroids(&config.output.join("centroids.bin"), &runtime_centroids)?;

    let meta = ArtifactMeta {
        version: ARTIFACT_VERSION,
        dimensions: DIMENSIONS,
        packed_dimensions: PACKED_DIMENSIONS,
        vector_count: vector_count as u64,
        cluster_count,
        probe_count: config.probes.min(cluster_count).max(1),
        list_offsets: offsets.iter().map(|value| *value as u64).collect(),
        list_lengths: counts,
    };
    fs::write(
        config.output.join("meta.json"),
        serde_json::to_vec_pretty(&meta)?,
    )
    .with_context(|| {
        format!(
            "failed to write {}",
            config.output.join("meta.json").display()
        )
    })?;

    println!(
        "built artifacts at {} with {} vectors across {} clusters",
        config.output.display(),
        vector_count,
        cluster_count
    );
    Ok(())
}

struct Config {
    input: PathBuf,
    output: PathBuf,
    clusters: usize,
    probes: usize,
}

impl Config {
    fn from_env() -> Result<Self> {
        let mut input = PathBuf::from(DEFAULT_INPUT);
        let mut output = PathBuf::from(DEFAULT_OUTPUT);
        let mut clusters = DEFAULT_CLUSTERS;
        let mut probes = DEFAULT_PROBES;

        let args: Vec<String> = env::args().collect();
        let mut idx = 1;
        while idx < args.len() {
            match args[idx].as_str() {
                "--input" => {
                    idx += 1;
                    input = PathBuf::from(args.get(idx).context("missing value for --input")?);
                }
                "--output" => {
                    idx += 1;
                    output = PathBuf::from(args.get(idx).context("missing value for --output")?);
                }
                "--clusters" => {
                    idx += 1;
                    clusters = args
                        .get(idx)
                        .context("missing value for --clusters")?
                        .parse()
                        .context("invalid --clusters value")?;
                }
                "--probes" => {
                    idx += 1;
                    probes = args
                        .get(idx)
                        .context("missing value for --probes")?
                        .parse()
                        .context("invalid --probes value")?;
                }
                flag => return Err(anyhow!("unknown flag {flag}")),
            }
            idx += 1;
        }

        Ok(Self {
            input,
            output,
            clusters,
            probes,
        })
    }
}

struct LoadedReferences {
    vectors: Vec<i8>,
    labels: Vec<u8>,
}

fn load_references(path: &Path) -> Result<LoadedReferences> {
    let file = File::open(path).with_context(|| format!("failed to open {}", path.display()))?;
    let reader: Box<dyn Read> = if path.extension().and_then(|ext| ext.to_str()) == Some("gz") {
        Box::new(flate2::read::GzDecoder::new(BufReader::new(file)))
    } else {
        Box::new(BufReader::new(file))
    };
    let mut deserializer = serde_json::Deserializer::from_reader(reader);
    let visitor = ReferencesVisitor::default();
    Ok(deserializer.deserialize_seq(visitor)?)
}

#[derive(Default)]
struct ReferencesVisitor {
    vectors: Vec<i8>,
    labels: Vec<u8>,
}

impl<'de> Visitor<'de> for ReferencesVisitor {
    type Value = LoadedReferences;

    fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
        formatter.write_str("a JSON array of reference vectors")
    }

    fn visit_seq<A>(mut self, mut seq: A) -> Result<Self::Value, A::Error>
    where
        A: SeqAccess<'de>,
    {
        while let Some(record) = seq.next_element::<ReferenceRecord>()? {
            validate_reference_record(&record).map_err(serde::de::Error::custom)?;
            let quantized = quantize_vector_padded(&record.vector);
            self.vectors.extend_from_slice(&quantized);
            self.labels.push((record.label == "fraud") as u8);
        }

        Ok(LoadedReferences {
            vectors: self.vectors,
            labels: self.labels,
        })
    }
}

fn sampled_indices(vector_count: usize, sample_count: usize) -> Vec<usize> {
    if sample_count >= vector_count {
        return (0..vector_count).collect();
    }

    let stride = vector_count / sample_count;
    (0..sample_count).map(|idx| idx * stride).collect()
}

fn fit_centroids(
    vectors: &[i8],
    sample_indices: &[usize],
    cluster_count: usize,
) -> Vec<[f32; PACKED_DIMENSIONS]> {
    let mut centroids = initial_centroids(vectors, sample_indices, cluster_count);
    for _ in 0..KMEANS_ITERATIONS {
        let mut sums = vec![[0.0_f32; PACKED_DIMENSIONS]; cluster_count];
        let mut counts = vec![0_u32; cluster_count];

        for &sample_idx in sample_indices {
            let vector = slice_vector(vectors, sample_idx);
            let cluster_idx = nearest_centroid(vector, &centroids);
            counts[cluster_idx] += 1;
            for dim in 0..PACKED_DIMENSIONS {
                sums[cluster_idx][dim] += vector[dim] as f32 / 127.0;
            }
        }

        for cluster_idx in 0..cluster_count {
            if counts[cluster_idx] == 0 {
                continue;
            }
            for dim in 0..PACKED_DIMENSIONS {
                centroids[cluster_idx][dim] = sums[cluster_idx][dim] / counts[cluster_idx] as f32;
            }
        }
    }
    centroids
}

fn initial_centroids(
    vectors: &[i8],
    sample_indices: &[usize],
    cluster_count: usize,
) -> Vec<[f32; PACKED_DIMENSIONS]> {
    let sample_len = sample_indices.len();
    (0..cluster_count)
        .map(|cluster_idx| {
            let sample_idx = sample_indices[cluster_idx * sample_len / cluster_count];
            let mut centroid = [0.0_f32; PACKED_DIMENSIONS];
            for dim in 0..PACKED_DIMENSIONS {
                centroid[dim] = slice_vector(vectors, sample_idx)[dim] as f32 / 127.0;
            }
            centroid
        })
        .collect()
}

fn assign_vectors(vectors: &[i8], centroids: &[[f32; PACKED_DIMENSIONS]]) -> Vec<usize> {
    let vector_count = vectors.len() / PACKED_DIMENSIONS;
    let mut assignments = Vec::with_capacity(vector_count);
    for idx in 0..vector_count {
        assignments.push(nearest_centroid(slice_vector(vectors, idx), centroids));
    }
    assignments
}

fn cluster_counts(assignments: &[usize], cluster_count: usize) -> Vec<u32> {
    let mut counts = vec![0_u32; cluster_count];
    for &cluster_idx in assignments {
        counts[cluster_idx] += 1;
    }
    counts
}

fn prefix_offsets(counts: &[u32]) -> Vec<usize> {
    let mut offsets = Vec::with_capacity(counts.len());
    let mut cursor = 0_usize;
    for &count in counts {
        offsets.push(cursor);
        cursor += count as usize;
    }
    offsets
}

fn reorder_by_cluster(
    vectors: &[i8],
    labels: &[u8],
    assignments: &[usize],
    offsets: &[usize],
) -> (Vec<i8>, Vec<u8>) {
    let vector_count = labels.len();
    let mut ordered_vectors = vec![0_i8; vectors.len()];
    let mut ordered_labels = vec![0_u8; vector_count];
    let mut positions = offsets.to_vec();

    for original_idx in 0..vector_count {
        let cluster_idx = assignments[original_idx];
        let position = positions[cluster_idx];
        positions[cluster_idx] += 1;

        let src_start = original_idx * PACKED_DIMENSIONS;
        let dst_start = position * PACKED_DIMENSIONS;
        ordered_vectors[dst_start..dst_start + PACKED_DIMENSIONS]
            .copy_from_slice(&vectors[src_start..src_start + PACKED_DIMENSIONS]);
        ordered_labels[position] = labels[original_idx];
    }

    (ordered_vectors, ordered_labels)
}

fn recompute_runtime_centroids(
    ordered_vectors: &[i8],
    counts: &[u32],
    offsets: &[usize],
    fallback: &[[f32; PACKED_DIMENSIONS]],
) -> Vec<[f32; PACKED_DIMENSIONS]> {
    let mut centroids = vec![[0.0_f32; PACKED_DIMENSIONS]; counts.len()];
    for cluster_idx in 0..counts.len() {
        let count = counts[cluster_idx] as usize;
        if count == 0 {
            centroids[cluster_idx] = fallback[cluster_idx];
            continue;
        }
        let start = offsets[cluster_idx];
        for item_idx in 0..count {
            let base = (start + item_idx) * PACKED_DIMENSIONS;
            for dim in 0..PACKED_DIMENSIONS {
                centroids[cluster_idx][dim] += ordered_vectors[base + dim] as f32 / 127.0;
            }
        }
        for dim in 0..PACKED_DIMENSIONS {
            centroids[cluster_idx][dim] /= count as f32;
        }
    }
    centroids
}

fn write_vectors(path: &Path, vectors: &[i8]) -> Result<()> {
    let mut file =
        File::create(path).with_context(|| format!("failed to create {}", path.display()))?;
    let bytes: Vec<u8> = vectors.iter().map(|value| *value as u8).collect();
    file.write_all(&bytes)
        .with_context(|| format!("failed to write {}", path.display()))
}

fn write_labels(path: &Path, labels: &[u8]) -> Result<()> {
    fs::write(path, labels).with_context(|| format!("failed to write {}", path.display()))
}

fn write_centroids(path: &Path, centroids: &[[f32; PACKED_DIMENSIONS]]) -> Result<()> {
    let mut file =
        File::create(path).with_context(|| format!("failed to create {}", path.display()))?;
    for centroid in centroids {
        for component in centroid {
            file.write_all(&component.to_le_bytes())
                .with_context(|| format!("failed to write {}", path.display()))?;
        }
    }
    Ok(())
}

fn nearest_centroid(vector: &[i8], centroids: &[[f32; PACKED_DIMENSIONS]]) -> usize {
    let mut best_idx = 0_usize;
    let mut best_distance = f32::MAX;
    for (idx, centroid) in centroids.iter().enumerate() {
        let mut distance = 0.0_f32;
        for dim in 0..PACKED_DIMENSIONS {
            let delta = vector[dim] as f32 / 127.0 - centroid[dim];
            distance += delta * delta;
        }
        if distance < best_distance {
            best_distance = distance;
            best_idx = idx;
        }
    }
    best_idx
}

fn slice_vector(vectors: &[i8], index: usize) -> &[i8] {
    let start = index * PACKED_DIMENSIONS;
    &vectors[start..start + PACKED_DIMENSIONS]
}

#[allow(dead_code)]
#[derive(Deserialize)]
struct _Ignore(pub IgnoredAny);
