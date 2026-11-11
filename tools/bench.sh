#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

CORPUS="${1:-corpus/fixtures}"
QUERIES="${2:-queries/wikipedia.txt}"
BIN=./build/search

if [ ! -x "$BIN" ]; then
  echo "build first: cmake -S . -B build && cmake --build build -j" >&2
  exit 1
fi
if [ ! -e "$CORPUS" ]; then
  echo "no such corpus: $CORPUS" >&2
  exit 1
fi
if [ ! -f "$QUERIES" ]; then
  echo "no such query file: $QUERIES" >&2
  exit 1
fi

INDEX=$(mktemp -t search-bench-XXXXXX)
trap 'rm -f "$INDEX" "$INDEX".block*' EXIT

field() { grep "^$1 " | cut -d' ' -f2-; }
cores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

peak_rss() {
  local out
  out=$(/usr/bin/time -l "$@" 2>&1 >/dev/null | awk '/maximum resident/ {print $1}') || true
  if [ -z "$out" ]; then
    out=$(/usr/bin/time -v "$@" 2>&1 >/dev/null \
          | awk '/Maximum resident/ {print $NF * 1024}') || true
  fi
  echo "${out:-0}"
}

echo "# Benchmark"
echo
echo "- corpus: \`$CORPUS\`"
echo "- queries: \`$QUERIES\`"
echo "- date: $(date -u '+%Y-%m-%d %H:%M UTC')"
echo "- machine: $(uname -sm)$( [ "$(uname -s)" = Darwin ] && printf ', %s' "$(sysctl -n machdep.cpu.brand_string 2>/dev/null || true)" )"
echo "- cores: $cores"
echo

echo "indexing (1 thread)..." >&2
BUILD1=$("$BIN" bench build --threads 1 "$CORPUS")
echo "indexing ($cores threads)..." >&2
BUILDN=$("$BIN" bench build --threads "$cores" "$CORPUS")

echo "writing the index..." >&2
BUILD_RSS=$(peak_rss "$BIN" index-write --threads "$cores" "$CORPUS" "$INDEX")

echo "measuring queries..." >&2
QUERY=$("$BIN" bench query --repeats 5 "$INDEX" "$QUERIES")
SNIPPET=$("$BIN" bench query --repeats 5 --snippet --corpus "$CORPUS" "$INDEX" "$QUERIES" \
          2>/dev/null || true)
SERVE_RSS=$(peak_rss "$BIN" bench query --repeats 5 "$INDEX" "$QUERIES")

echo "measuring one cold-start query..." >&2
first=$(head -n 1 "$QUERIES")
start=$(date +%s%N 2>/dev/null || echo 0)
"$BIN" query --limit 10 "$INDEX" "$first" >/dev/null
end=$(date +%s%N 2>/dev/null || echo 0)
startup_ms=$(awk -v a="$start" -v b="$end" 'BEGIN { printf "%.0f", (b - a) / 1000000 }')

echo "measuring the block build..." >&2
docs=$(echo "$BUILD1" | field documents)
block=$(( docs / 10 + 1 ))
EXT=$("$BIN" index-build --block "$block" "$CORPUS" "$INDEX.ext")
EXT_RSS=$(peak_rss "$BIN" index-build --block "$block" "$CORPUS" "$INDEX.ext")
if cmp -s "$INDEX" "$INDEX.ext"; then IDENTICAL="yes"; else IDENTICAL="NO"; fi
rm -f "$INDEX.ext" "$INDEX.ext".block*

echo "measuring compression..." >&2
SIZES=$("$BIN" index-size "$INDEX")

ms1=$(echo "$BUILD1" | field build_ms)
msN=$(echo "$BUILDN" | field build_ms)
speedup=$(awk -v a="$ms1" -v b="$msN" 'BEGIN { if (b > 0) printf "%.2f", a / b; else print "n/a" }')
gb() { awk -v b="$1" 'BEGIN { printf "%.2f", b / 1073741824 }'; }
mb() { awk -v b="$1" 'BEGIN { printf "%.0f", b / 1048576 }'; }

echo "Documents: $docs"
echo "Distinct terms: $(echo "$BUILD1" | field terms)"
echo "Postings: $(echo "$BUILD1" | field postings)"
echo "Corpus bytes: $(echo "$BUILD1" | field source_bytes)"
echo "Build, 1 thread: $ms1 ms"
echo "Build, $cores threads: $msN ms"
echo "Parallel speedup: ${speedup}x"
echo "Build, of which corpus open: $(echo "$BUILDN" | field open_ms) ms"
echo "Build, of which parallel indexing: $(echo "$BUILDN" | field index_ms) ms"
echo "Build, of which serial merge: $(echo "$BUILDN" | field merge_ms) ms"
echo "Ingest rate: $(echo "$BUILDN" | field documents_per_second) docs/s, $(echo "$BUILDN" | field megabytes_per_second) MB/s"
echo "Peak memory, in-memory build: $(gb "$BUILD_RSS") GB"
echo "Peak memory, block build: $(gb "$EXT_RSS") GB"
echo "Block build, blocks used: $(echo "$EXT" | field blocks)"
echo "Block build, byte-identical to in-memory build: $IDENTICAL"
echo "Index, uncompressed: $(echo "$SIZES" | awk '/^plain/ {print $2}') bytes"
echo "Index, delta + varbyte: $(echo "$SIZES" | awk '/^varbyte/ {print $2}') bytes"
echo "Compression ratio: $(echo "$SIZES" | field ratio)x"
echo "Startup, one query, whole process: ${startup_ms} ms"
echo "Query latency p50: $(echo "$QUERY" | field p50_ms) ms"
echo "Query latency p95: $(echo "$QUERY" | field p95_ms) ms"
echo "Query latency p99: $(echo "$QUERY" | field p99_ms) ms"
echo "Query throughput: $(echo "$QUERY" | field queries_per_second) queries/s"
if [ -n "$SNIPPET" ]; then
echo "p50, with snippets: $(echo "$SNIPPET" | field p50_ms) ms"
echo "p95, with snippets: $(echo "$SNIPPET" | field p95_ms) ms"
fi
echo "Peak memory, serving: $(mb "$SERVE_RSS") MB"
echo "Queries measured: $(echo "$QUERY" | field queries)"
