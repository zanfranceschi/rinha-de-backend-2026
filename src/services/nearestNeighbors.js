"use strict";

const DEFAULT_TOP_K = 5;

const placeholderDataset = [
  {
    id: "ref-1",
    vector: [0, 0, 0],
    metadata: { label: "placeholder" },
  },
  {
    id: "ref-2",
    vector: [1, 0, 0],
    metadata: { label: "placeholder" },
  },
];

function similarityStub(vectorA, vectorB) {
  // Placeholder until a real similarity metric is selected.
  if (!Array.isArray(vectorA) || !Array.isArray(vectorB)) {
    return 0;
  }
  if (vectorA.length !== vectorB.length) {
    return 0;
  }
  return 0;
}

function normalizeTopK(value) {
  return Number.isInteger(value) && value > 0 ? value : DEFAULT_TOP_K;
}

function getNearestNeighbors(queryVector, dataset = placeholderDataset, options = {}) {
  if (!Array.isArray(queryVector)) {
    throw new TypeError("queryVector must be an array of numbers");
  }
  if (!Array.isArray(dataset)) {
    throw new TypeError("dataset must be an array");
  }

  const topK = normalizeTopK(options.topK);
  const similarity = typeof options.similarity === "function" ? options.similarity : similarityStub;

  const scored = dataset.map((item) => {
    const vector = Array.isArray(item.vector) ? item.vector : [];
    const score = similarity(queryVector, vector);
    return { ...item, score };
  });

  scored.sort((a, b) => b.score - a.score);

  return scored.slice(0, topK);
}

module.exports = {
  DEFAULT_TOP_K,
  placeholderDataset,
  similarityStub,
  getNearestNeighbors,
};
