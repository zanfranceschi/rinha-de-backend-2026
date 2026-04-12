#!/usr/bin/env bash
set -euo pipefail

TEST_FILE="test/test.js"

start_rate=$(grep -oP 'startRate:\s*\K\d+' "$TEST_FILE")

durations=($(grep -oP "duration:\s*'\K[0-9]+" "$TEST_FILE"))
targets=($(grep -oP "target:\s*\K\d+" "$TEST_FILE"))

# collect rows
rows=()
cumulative=0
total_duration=0
prev_rate=$start_rate

for i in "${!durations[@]}"; do
    d=${durations[$i]}
    t=${targets[$i]}
    stage_reqs=$(( (prev_rate + t) * d / 2 ))
    cumulative=$((cumulative + stage_reqs))
    rows+=("$((i+1))|${d}s|$prev_rate|$t|$stage_reqs|$cumulative")
    prev_rate=$t
    total_duration=$((total_duration + d))
done

total_row="total|${total_duration}s|$start_rate|$prev_rate||$cumulative"

# compute column widths
headers=("stage" "duration" "from" "to" "stage_reqs" "cumulative")
widths=()
for h in "${headers[@]}"; do
    widths+=(${#h})
done

for row in "${rows[@]}" "$total_row"; do
    IFS='|' read -ra cols <<< "$row"
    for j in "${!cols[@]}"; do
        len=${#cols[$j]}
        (( len > widths[j] )) && widths[$j]=$len
    done
done

# drawing helpers
hline() {
    local left=$1 mid=$2 right=$3
    printf "%s" "$left"
    for j in "${!widths[@]}"; do
        printf "%s" "$(printf '─%.0s' $(seq 1 $((widths[j] + 2))))"
        [[ $j -lt $((${#widths[@]} - 1)) ]] && printf "%s" "$mid"
    done
    printf "%s\n" "$right"
}

print_row() {
    IFS='|' read -ra cols <<< "$1"
    printf "│"
    for j in "${!widths[@]}"; do
        printf " %*s │" "${widths[$j]}" "${cols[$j]}"
    done
    printf "\n"
}

# render
hline "┌" "┬" "┐"
printf "│"
for j in "${!headers[@]}"; do
    printf " %-*s │" "${widths[$j]}" "${headers[$j]}"
done
printf "\n"
hline "├" "┼" "┤"

for row in "${rows[@]}"; do
    print_row "$row"
done

hline "├" "┼" "┤"
print_row "$total_row"
hline "└" "┴" "┘"
