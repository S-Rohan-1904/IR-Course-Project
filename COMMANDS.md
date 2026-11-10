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
./build/search query --scorer bm25f <index.idx> 'solar system'   # title-weighted
./build/search query --pagerank 0.1 <crawl.idx> 'create table'     # link authority
./build/search query --pagerank 0.25 --pagerank-graph site <crawl.idx> 'python'   # per site
./build/search query --pagerank 0.25 --pagerank-graph site --pagerank-max-words 2 <crawl.idx> 'python'
./build/search query --pagerank 0.25 --pagerank-graph home <crawl.idx> 'python'   # every home page alike
./build/search repl <index.idx>      # :scorer, :pagerank, :pagerank-graph, :pagerank-max-words, :complete, did-you-mean
./build/search serve --port 8080 <index.idx>   # http://127.0.0.1:8080/
curl 'http://127.0.0.1:8080/api/search?q=python&pagerank=0.25&graph=site&max_words=2'
```

## Crawl

```bash
./build/search crawl --delay 1000 --max-pages 2000 --prefix /3/ \
    --out corpus/crawl/pydocs https://docs.python.org/3/
./tools/merge_crawls.py corpus/crawl/docs corpus/crawl/pydocs corpus/crawl/postgres
./build/search index-write corpus/crawl/docs docs.idx   # also writes .pagerank, .siterank and .urls
```

## Spelling and completion

```bash
./build/search suggest <index.idx> recieve
./build/search complete <index.idx> astro
```

`suggest` lists indexed terms within two edits of a misspelling, using a BK-tree
over the dictionary. `complete` lists indexed terms that begin with a prefix,
from a trie, most frequent first.

## Relevance (BEIR)

```bash
./build/search beir-import <corpus.jsonl> <out.corpus>
./build/search evaluate <index.idx> <queries.jsonl> <qrels.tsv>
./build/search evaluate --cutoff 5 <index.idx> <queries.jsonl> <qrels.tsv>
./build/search evaluate --scorer bm25f <index.idx> <queries.jsonl> <qrels.tsv>
./build/search evaluate --scorer tfidf <index.idx> <queries.jsonl> <qrels.tsv>
```
