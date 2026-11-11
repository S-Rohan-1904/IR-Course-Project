# A Search Engine for the Web

A search engine with a web crawler, link analysis and ranked retrieval, written from scratch in C++20 with no third party library.

Course project for **CS F469 Information Retrieval**, Group 3.

| ID            | Name                     |
| ------------- | ------------------------ |
| 2023A7PS0562P | Nainika Kalra            |
| 2023B2A71017P | Dhayanidhi Purushothaman |
| 2023B2A70935P | Rohan S                  |

## Overview

1. **Acquire**: import Simple English Wikipedia and BEIR datasets, or crawl web pages.
2. **Analyze**: tokenize, normalize, remove stopwords and stem.
3. **Index**: build a compressed positional inverted index, in parallel or in blocks for corpora larger than memory.
4. **Retrieve**: answer boolean and phrase queries and rank them with TF-IDF, BM25 or BM25F, optionally scaled by PageRank.
5. **Evaluate**: measure ranking quality on BEIR with nDCG, precision, recall, F1, MRR and MAP, and measure speed and memory with a benchmark script.

Everything is built from scratch(no external libraries)

## Quick start

Needs CMake 3.20 or newer and a C++20 compiler. [SETUP.md](SETUP.md) has the full walkthrough, including every install step and troubleshooting.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

```bash
./tools/fetch_wikipedia.sh simple --yes
```

```bash
./build/search index-write --threads 8 corpus/wikipedia/simplewiki.corpus corpus/wikipedia/simplewiki.idx
```

```bash
./build/search query --snippet --limit 3 corpus/wikipedia/simplewiki.idx 'solar system'
```

```
1. doc_00015284  15.9926
   ...Renewable energy Renewable resource [Solar] power "[Solar] [systems] projects"...
2. doc_00010853  15.8629
   Small [solar] [system] body A small [Solar] [System] body (SSSB) is a term defined ...
3. doc_00015295  15.5636
```

To use the frontend, run `./build/search serve corpus/wikipedia/simplewiki.idx` and open http://127.0.0.1:8080.

## Design

### Data and preprocessing

Wikipedia comes from the CirrusSearch dump (Simple English, 278,214 articles). BEIR datasets are read from their `corpus.jsonl`. Both importers write the same packed corpus format, one file with a table of offsets, so reading a document is a seek and not an `open` call. Crawled pages go through an HTML scanner that drops script and style tags, turns block tags into whitespace, and keeps the links for the link graph.

### Text analysis

Text is split on whitespace, and compounds are split again at hyphens, slashes and dashes. Each token keeps its position and byte offset. Tokens are then lowercased, stripped of punctuation and folded from Latin-1 and Latin Extended-A to plain ASCII. Stopwords are removed after that, and what is left goes through the Porter stemmer.

### Indexing

A positional inverted index stores (doc id, term frequency, positions) for each term. Doc ids and positions are stored as gaps and variable-byte encoded.

- **Parallel build:** The corpus is split into slices that are indexed on separate threads, then merged in slice order, so the file is byte-identical regardless of the thread count.
- **Block build:** For corpora larger than memory, `index-build` indexes a block of documents at a time and merges the blocks in one pass over sorted term streams. Its output is byte-identical to the in-memory build.
- **Incremental update:** `index-update` re-indexes only the documents whose content hash changed, and writes the same file a full rebuild would.
- **Loading:** Opening an index is an `mmap` plus a header read. Finding a term is a binary search, and a postings list is decoded only when a query needs it.

### Retrieval and ranking

Queries are parsed into a boolean tree with `AND`, `OR`, `NOT`, parentheses and `"quoted phrases"` (binding: NOT > AND > OR). Matched documents are scored with one of three functions:

- **TF-IDF**, the baseline.
- **BM25**, the default (k1 = 1.2, b = 0.75).
- **BM25F**, which splits each term's frequency into title and body by position, normalizes each by its own field length, and weights the title twice the body before saturating.

Scoring is sharded by document across threads, and the top k results are kept with a min-heap. Snippets are cut from the original text using the stored byte offsets.

A misspelled query term gets a "did you mean" from a BK-tree over the dictionary, and prefixes complete from a trie. Both are available in the REPL and in a small HTTP front end.

### Crawling

The crawler obeys `robots.txt`, limits its request rate per host, normalizes URLs, and skips pages whose content it has already seen. It fetches from many hosts in parallel while spacing requests to each one, caps pages per host, and can stay under a path prefix. It writes the pages as a corpus together with the link graph.

### Link analysis

PageRank scores each page by how many pages link to it, and how important those pages are. It runs on the links the crawler found, with 0.85 damping and 30 rounds. Pages with no outgoing links still pass on their score, and a page linking to the same page twice counts once.

With `--pagerank <w>` the text score is multiplied by `1 + w * ln(1 + N * pagerank)`, so link authority reorders matching documents but never adds a non-matching one. The graph can be pages, sites (each site's score given to its home page), or a flat boost to every home page as a control. `--pagerank-max-words` limits it to short queries.

## Architecture

![System architecture](docs/architecture.svg)

The top row builds the index, the middle row answers queries against it, and the bottom row is link analysis and evaluation. PageRank scores, when a weight is given, multiply the text score at query time.

### Source layout

All code is in `src/`. `main.cpp` is the command line, and `./build/search` with no arguments lists every command.

| Area                | Files                                                                                         |
| ------------------- | --------------------------------------------------------------------------------------------- |
| Corpus and import   | `corpus`, `corpus_file`, `document`, `wiki_import`, `beir`, `jsonl`                           |
| Text analysis       | `tokenizer`, `normalize`, `stopwords`, `stemmer`, `analyzer`                                  |
| Indexing            | `index`, `index_io`, `index_map`, `index_build`, `mapped_file`, `thread_pool`                 |
| Query and ranking   | `query`, `ranking`, `results`, `snippet`, `suggest`, `complete`, `edit_distance`, `server`    |
| Crawling            | `url`, `robots`, `html`, `frontier`, `fetcher`, `http`, `crawler`                             |
| Link analysis       | `pagerank`, `siterank`, `prior`                                                               |
| Evaluation          | `evaluator`, `bench`                                                                          |

Helper scripts are in `tools/`: `fetch_wikipedia.sh` downloads and imports Wikipedia, `bench.sh` runs the benchmark, `analyze.py` reports on a query set, and `merge_crawls.py` combines crawls.

## Results

Measured on an Apple M2 laptop (8 cores). See [SETUP.md](SETUP.md) to run the benchmark yourself.

### Retrieval quality

BEIR SciFact (5,183 abstracts, 300 test queries):

| Scorer            | nDCG@10   | P@5       | P@10      | R@10      | R@100     | F1@10     | MRR@10    | MAP       |
| ----------------- | --------- | --------- | --------- | --------- | --------- | --------- | --------- | --------- |
| TF-IDF (baseline) | 0.659     | 0.161     | 0.088     | 0.802     | 0.913     | 0.156     | 0.620     | 0.614     |
| BM25              | 0.685     | 0.163     | 0.091     | 0.824     | **0.925** | 0.162     | 0.645     | 0.641     |
| BM25F             | **0.693** | **0.164** | **0.091** | **0.827** | **0.925** | **0.163** | **0.656** | **0.650** |

BM25F matches or beats BM25, and BM25 beats TF-IDF, on every metric. Both BM25 scorers are above the 0.665 published for Lucene BM25 in the BEIR paper. Precision@10 is low because 277 of the 300 queries have exactly one relevant document, so the best possible score on this set is 0.113.

BEIR TREC-COVID (171,331 documents, 50 queries) gives nDCG@10 of 0.663 with BM25 and 0.685 with BM25F, against 0.656 published for BM25.

### Link analysis

- On a crawl of three documentation sites (2,424 pages), PageRank lowered MRR@10 at every weight, because inside a site the best linked pages are the indexes in every navigation bar.
- On a crawl of 1,096 independent sites, ranking sites instead of pages raised home-page finding from 0.565 to 0.775.
- A control that gave every home page the same boost, with no links at all, reached 0.851. Most of that gain is therefore a preference for home pages and not link structure.

Link authority is off by default for this reason.

### Time and memory

Simple English Wikipedia: 278,214 articles, 425 MB of text, 1.20 million distinct terms, 26.2 million postings. Query figures are for 25 keyword, boolean and phrase queries, 5 runs each, with the index already in memory.

| Measurement                                | Value                                              |
| ------------------------------------------ | -------------------------------------------------- |
| Index build, 1 thread / 8 threads          | 23.6 s / 8.5 s (2.8x faster)                       |
| Index size, uncompressed / delta + varbyte | 1,042 MB / 186 MB (5.6x smaller)                   |
| Peak memory, in-memory build / block build | 2.58 GB / 1.05 GB                                  |
| Peak memory, serving                       | 97 MB                                              |
| Startup to first answer                    | 27 ms                                              |
| Query latency p50 / p95 / p99              | 1.3 / 12.7 / 17.6 ms (p50 with snippets: 2.5 ms)   |
| Query throughput                           | 325 queries/s                                      |

The serial merge takes 2.1 s of the 8.5 s build, which is why 8 threads give 2.8x and not more.


## Team contributions

| Member                   | Area                 | Components |
| ------------------------ | -------------------- | ---------- |
| Rohan S                  | Storage and indexing | Corpus file format, Wikipedia and BEIR importers, positional inverted index with delta and varbyte compression, memory-mapped index format, parallel build with a thread pool, block-based external build, BEIR evaluation metrics, benchmark script, spelling correction, HTTP front end |
| Nainika Kalra            | Text and query       | Tokenizer, normalization, stopword removal, Porter stemmer, query parser, boolean and phrase matching, TF-IDF, BM25 and BM25F scoring, top-k selection, snippets, interactive search prompt, TREC-COVID evaluation |
| Dhayanidhi Purushothaman | Crawler and PageRank | URL queue and normalization, robots.txt and crawl delays, per-site rate limiting, HTML to text, duplicate detection, link graph output, PageRank over that graph and its combination with the text score, incremental re-indexing, prefix autocomplete, skip pointers |

## Documentation

- [SETUP.md](SETUP.md): from a fresh clone to a Wikipedia index, the benchmarks and the relevance scores, with troubleshooting.
- [COMMANDS.md](COMMANDS.md): quick reference for the common commands.
