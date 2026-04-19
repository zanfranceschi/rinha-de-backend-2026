import json
import numpy as np
import matplotlib.pyplot as plt

# Load data
with open("resources/references.json") as f:
    data = json.load(f)

vectors = np.array([d["vector"] for d in data])
labels = np.array([d["label"] for d in data])

DIMS = [
    "amount", "installments", "amt_vs_avg", "hour_of_day",
    "day_of_week", "min_since_last_tx", "km_from_last_tx",
    "km_from_home", "tx_count_24h", "is_online",
    "card_present", "is_new_merchant", "mcc_risk", "merchant_avg_amt"
]

fraud_mask = labels == "fraud"
legit_mask = labels == "legit"

# Clamp sentinel values (-1) to 0 so the radar stays in 0.0-1.0 range
vectors_clamped = np.clip(vectors, 0, 1)
fraud_mean = vectors_clamped[fraud_mask].mean(axis=0)
legit_mean = vectors_clamped[legit_mask].mean(axis=0)

angles = np.linspace(0, 2 * np.pi, len(DIMS), endpoint=False).tolist()
angles += angles[:1]  # close the polygon

fig = plt.figure(figsize=(10, 10))
ax = fig.add_axes([0.15, 0.1, 0.7, 0.7], polar=True)
fig.suptitle("14D Fraud Detection — Radar Chart (mean profiles)", fontsize=14, fontweight="bold")

ax.plot(angles, list(fraud_mean) + [fraud_mean[0]], "o-", color="#e74c3c", linewidth=2, label="fraud (mean)")
ax.fill(angles, list(fraud_mean) + [fraud_mean[0]], color="#e74c3c", alpha=0.15)
ax.plot(angles, list(legit_mean) + [legit_mean[0]], "o-", color="#3498db", linewidth=2, label="legit (mean)")
ax.fill(angles, list(legit_mean) + [legit_mean[0]], color="#3498db", alpha=0.15)

ax.set_ylim(0, 1)
ax.set_xticks(angles[:-1])
ax.set_xticklabels(DIMS, fontsize=9)
ax.legend(loc="upper right", bbox_to_anchor=(1.15, 1.1))

plt.savefig("visualization/fraud_14d_visualization.png", dpi=150)
print("Saved: fraud_14d_visualization.png")
plt.close()

# --- 20 individual fraud samples ---
fraud_vectors = vectors_clamped[fraud_mask]
rng = np.random.default_rng(42)
sample_indices = rng.choice(len(fraud_vectors), size=20, replace=False)

fig, axes = plt.subplots(4, 5, figsize=(25, 20), subplot_kw=dict(polar=True))
fig.suptitle("20 Individual Fraud Transactions", fontsize=16, fontweight="bold", y=1.02)

for i, ax in enumerate(axes.flat):
    v = fraud_vectors[sample_indices[i]]
    values = list(v) + [v[0]]

    ax.plot(angles, values, "o-", color="#e74c3c", linewidth=2)
    ax.fill(angles, values, color="#e74c3c", alpha=0.2)
    ax.set_ylim(0, 1)
    ax.set_xticks(angles[:-1])
    ax.set_xticklabels(DIMS, fontsize=6)
    ax.set_title(f"Fraud #{i+1}", fontsize=10, pad=15)

plt.tight_layout()
plt.savefig("visualization/fraud_14d_individual.png", dpi=150, bbox_inches="tight")
print("Saved: fraud_14d_individual.png")
plt.close()

# --- 20 individual legit samples ---
legit_vectors = vectors_clamped[legit_mask]
legit_sample_indices = rng.choice(len(legit_vectors), size=20, replace=False)

fig, axes = plt.subplots(4, 5, figsize=(25, 20), subplot_kw=dict(polar=True))
fig.suptitle("20 Individual Legit Transactions", fontsize=16, fontweight="bold", y=1.02)

for i, ax in enumerate(axes.flat):
    v = legit_vectors[legit_sample_indices[i]]
    values = list(v) + [v[0]]

    ax.plot(angles, values, "o-", color="#3498db", linewidth=2)
    ax.fill(angles, values, color="#3498db", alpha=0.2)
    ax.set_ylim(0, 1)
    ax.set_xticks(angles[:-1])
    ax.set_xticklabels(DIMS, fontsize=6)
    ax.set_title(f"Legit #{i+1}", fontsize=10, pad=15)

plt.tight_layout()
plt.savefig("visualization/legit_14d_individual.png", dpi=150, bbox_inches="tight")
print("Saved: legit_14d_individual.png")
plt.close()
