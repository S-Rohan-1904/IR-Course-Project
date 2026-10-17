# Commands

Quick reference. Full walkthrough in [SETUP.md](SETUP.md).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Benchmark

```bash
./tools/bench.sh <corpus> [query_file]

./tools/bench.sh corpus/fixtures queries/fixtures.txt   # ~5s, sanity check
./tools/bench.sh corpus/wikipedia/simplewiki.corpus     # ~4 min, full table
```

Prints build time at one and all threads, the phase breakdown, compression,
query p50/p95/p99, and peak memory.

## Inspect the analysis chain

```bash
./build/search analyze 'The Cats were Running'
```

Prints position, byte offset and final term for each token that survives
tokenization, normalization, stopword removal and stemming.

## Build an index

```bash
./build/search index-write --threads 8 <corpus> <index.idx>
```

## Query

```bash
./build/search query --snippet <index.idx> 'solar system'
./build/search repl <index.idx>
```

## Relevance (BEIR)

```bash
./build/search beir-import <corpus.jsonl> <out.corpus>
./build/search evaluate <index.idx> <queries.jsonl> <qrels.tsv>
./build/search evaluate --cutoff 5 <index.idx> <queries.jsonl> <qrels.tsv>
./build/search evaluate --scorer tfidf <index.idx> <queries.jsonl> <qrels.tsv>
```
