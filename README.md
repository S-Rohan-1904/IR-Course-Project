# A Search Engine for the Web

Crawling, link analysis and ranking, written from scratch in C++20. No Lucene,
no Elasticsearch, no third-party index, tokenizer or ranking library. The
dependencies are the C++ standard library, POSIX `mmap`, and libcurl, which is
optional and used only by the crawler.

Course project for **CS F469 Information Retrieval**, Group 3.

| ID | Name |
|---|---|
| 2023A7PS0562P | Nainika Kalra |
| 2023B1A70863P | Prachi Mittal |
| 2023B2A71017P | Dhayanidhi Purushothaman |
| 2023B2A70935P | Rohan S |

## What it does today

```console
$ ./build/search wiki-import simplewiki.json.gz simplewiki.corpus
$ ./build/search index-write --threads 8 simplewiki.corpus simplewiki.idx
$ ./build/search query --snippet simplewiki.idx 'solar system'
1. doc_00015284  16.1311
   ...Renewable energy Renewable resource [Solar] power "[Solar] [systems]
   projects"...
```

**Crawler.** robots.txt parsing with per-agent rules and crawl delay, a BFS
frontier with URL normalization and a seen set, HTML to text extraction,
per-host rate limiting, content-hash duplicate detection, and a link graph
written out for link analysis. Without libcurl the crawler still runs against a
mirrored site on disk, which is what the tests use.

**Text analysis.** Tokenizer that records the byte offset of every token,
Unicode-aware normalization that folds Latin-1 and Latin Extended-A to ASCII
(`Zürich` becomes `zurich`), stopword removal, and a Porter stemmer implemented
step by step with each step addressable from the command line.

**Index.** Positional inverted index: document id, term frequency and token
positions per posting. Delta encoded and variable-byte compressed. Built in
parallel across a thread pool with a deterministic merge, so the output is the
same bytes for any thread count. The on-disk format stores fixed-width tables
for the per-document arrays and a sorted table of term offsets, so opening an
index is an `mmap` and a header read, finding a term is a binary search over
mapped bytes, and a postings list is decoded only when a query asks for it.

**Query language.** `AND`, `OR`, `NOT`, parentheses and `"quoted phrases"`,
lexed and parsed into a boolean tree and evaluated as set operations over
sorted document id lists. Phrase matching walks the stored positions.

**Ranking.** TF-IDF and BM25, scored in parallel across document shards so that
floating-point addition order does not depend on the thread count. Top-k is a
min-heap. Snippets are cut from the original document bytes using the token
offsets, so results show the document's own capitalization and punctuation.

**Link analysis.** PageRank by power iteration over a crawl's link graph, with
a damping factor and dangling-node handling.

**Measurement.** `evaluate` reports nDCG@10, precision@10 and recall@100 against
BEIR queries and qrels. `bench build` and `bench query` measure construction and
latency. `index-size` reports the encoded size under each encoding, so the
compression claim is checkable rather than asserted.

## Status

Implemented and tested: crawler, text analysis, positional index, compression,
memory-mapped index format, parallel build, boolean and phrase queries, TF-IDF
and BM25 ranking, snippets, PageRank as a standalone stage, BEIR evaluation and
the benchmark harness.

Still to do:

- Combining PageRank with the BM25 text score in the final ranking. PageRank is
  computed today but not yet mixed into query results.
- Spelling correction and prefix autocomplete over the index vocabulary.
- Indexing a corpus larger than memory, by building in blocks and merging them.
- Incremental re-indexing of only the documents whose content changed.
- Dynamic pruning (WAND) for disjunctive queries, if profiling shows scoring
  rather than postings decoding is the cost.
- Field weighting so that a title match counts for more than a body match.
- Relevance runs on a second BEIR dataset, and a written comparison against the
  published BM25 baselines.

## Building

Needs CMake 3.20 or newer, a C++20 compiler, and Python 3. libcurl is optional.

```console
$ cmake -S . -B build && cmake --build build -j
$ python3 tests/runner.py
```

The tests are end-to-end: each case runs the real binary and compares its
output, so nothing passes because a mock agreed with it.

[SETUP.md](SETUP.md) goes from a fresh clone to a Wikipedia index.
[COMMANDS.md](COMMANDS.md) lists every command.
