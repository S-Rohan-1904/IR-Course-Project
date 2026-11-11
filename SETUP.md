# Setting up

This guide sets up the search engine over Simple English Wikipedia, then on to the benchmarks and the relevance scores.

- Run every command from the top folder of the repository
- Look in [Troubleshooting](#troubleshooting) for any problems that occur during the setup.

---

## Step 1. Install the tools

- Git
- CMake 3.20 or newer
- C++20 compiler
- curl
- Python 3 (for running helper scripts)
- libcurl (without it everything works except fetching web pages in the crawler)

**macOS**

```bash
xcode-select --install
```

```bash
brew install cmake curl python
```

**Check Version**

```bash
cmake --version
```

This project requires CMake 3.20 or newer.

**Ubuntu or Debian**

```bash
sudo apt update && sudo apt install -y git cmake g++ curl unzip python3 libcurl4-openssl-dev
```

**Check Version**

```bash
cmake --version
```

This project requires CMake 3.20 or newer.

## Step 2. Get the code

Skip this step if you already have cloned the repo.

```bash
git clone https://github.com/S-Rohan-1904/IR-Course-Project.git
```

```bash
cd IR-Course-Project
```

## Step 3. Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

**Check**

```bash
./build/search
```

It prints `usage: search <command> [args...]` followed by every command, grouped under preprocessing, indexing, retrieval, evaluation and inspection.

If the build stops with an error, see [Build errors](#build-errors). On some Macs the default compiler is broken, and the fix is one extra install which can be found using the above link.

## Step 4. Try it on the small test corpus

The repository includes 30 short handwritten documents in `corpus/fixtures`, so you can check everything works in this small dataset before downloading the large wikipedia dataset.

Build an index:

```bash
./build/search index-write corpus/fixtures fixtures.idx
```

This creates `fixtures.idx`. Look at stats of the index

```bash
./build/search index-stats fixtures.idx
```

```
documents: 30
terms: 235
postings: 343
```

`--snippet` flag indicates search with snippets.
`--corpus` flag indicates where the original text is (the index doesn't store it)

```bash
./build/search query --snippet --corpus corpus/fixtures --limit 3 fixtures.idx 'cat'
```

```
1. doc_003  2.5076
   The Black [Cat] A black [cat] crossed the road at night. The black [cat] was hunting a mouse. ...
2. doc_001  2.3900
   [Cats] and Mats The [cat] sat on the mat. The [cat] was happy on that mat. ...
3. doc_004  2.1294
   Dogs and [Cats] Together Dogs and [cats] can live together in one house. ...
```

Queries can include `AND`, `OR`, `NOT`, parentheses and `"quoted phrases"`:

```bash
./build/search query fixtures.idx '"black cat" OR dog'
```

For an interactive prompt, start the REPL, type queries, and type `:quit` to leave. 
`:help` lists the settings you can change there.

```bash
./build/search repl fixtures.idx
```

### Incremental updates

`index-update` re-indexes only the documents that were added, changed or removed, and writes the same file a full rebuild would. Try it on a copy of the fixtures:

```bash
cp -r corpus/fixtures corpus/mydocs
```

```bash
./build/search index-write corpus/mydocs mydocs.idx
```

Create a new file named `doc_031.txt` with some content. Its first line must be `title: ` followed by a title, then a blank line, then the text content.

```bash
rm corpus/mydocs/doc_030.txt
```

```bash
./build/search index-update corpus/mydocs mydocs.idx
```

```
added 1
updated 0
removed 1
unchanged 29
```

```bash
./build/search query mydocs.idx <word from the new file>
```

This prints `1. doc_031` and a score.

## Step 5. Download and import Wikipedia

```bash
./tools/fetch_wikipedia.sh simple --yes
```

This finds the newest Simple English Wikipedia dump, downloads it (640 MB) into `corpus/wikipedia/`, and imports it into one packed file, `corpus/wikipedia/simplewiki.corpus`. 

## Step 6. Build the Wikipedia index

```bash
./build/search index-write --threads 8 corpus/wikipedia/simplewiki.corpus corpus/wikipedia/simplewiki.idx
```

Check it:

```bash
./build/search index-stats corpus/wikipedia/simplewiki.idx
```

```
documents: 278214
terms: 1204562
postings: 26162711
```

This build needs about 3 GB of memory. On a machine with less memory use `--block`.

```bash
./build/search index-build --block 5000 corpus/wikipedia/simplewiki.corpus corpus/wikipedia/simplewiki.idx
```

Smaller blocks use less memory but take a little longer.

## Step 7. Search Wikipedia Index

```bash
./build/search query --snippet --limit 3 corpus/wikipedia/simplewiki.idx 'solar system'
```

```
1. doc_00015284  15.9926
   ...Renewable energy Renewable resource [Solar] power "[Solar] [systems] projects"...
2. doc_00010853  15.8629
   Small [solar] [system] body A small [Solar] [System] body (SSSB) is a term defined ...
3. doc_00015295  15.5636
   ...
```

### Spelling suggestions and prefix completion:

```bash
./build/search suggest corpus/wikipedia/simplewiki.idx 'astronmy'
```

```bash
./build/search complete corpus/wikipedia/simplewiki.idx 'photosyn'
```

### Serve frontend:

```bash
./build/search serve corpus/wikipedia/simplewiki.idx
```

The frontend will be served in [http://127.0.0.1:8080](http://127.0.0.1:8080)

## Step 8. Run the benchmarks

```bash
./tools/bench.sh corpus/wikipedia/simplewiki.corpus
```
To see what each query returned (needs Python 3):

```bash
./tools/analyze.py corpus/wikipedia/simplewiki.idx
```

This runs the queries in `queries/wikipedia.txt` and reports latency per query, how many results each found, which found nothing, and the score spread.

## Step 9. Measure relevance on BEIR SciFact

[BEIR](https://github.com/beir-cellar/beir) publishes corpora, test queries and relevance judgements, plus BM25 baselines to compare against. SciFact is small (3 MB, 5,183 abstracts, 300 test queries).

```bash
mkdir -p corpus/beir && curl -L -o corpus/beir/scifact.zip https://public.ukp.informatik.tu-darmstadt.de/thakur/BEIR/datasets/scifact.zip
```

```bash
unzip -o -q corpus/beir/scifact.zip -d corpus/beir
```

```bash
./build/search beir-import corpus/beir/scifact/corpus.jsonl corpus/beir/scifact.corpus
```

```bash
./build/search index-write corpus/beir/scifact.corpus corpus/beir/scifact.idx
```

```bash
./build/search evaluate corpus/beir/scifact.idx corpus/beir/scifact/queries.jsonl corpus/beir/scifact/qrels/test.tsv
```

```
queries 300
unjudged 809
scorer bm25
cutoff 10
ndcg_at_10 0.6845
precision_at_10 0.0910
max_precision_at_10 0.1130
recall_at_10 0.8239
f1_at_10 0.1619
mrr_at_10 0.6449
map 0.6407
r_precision 0.5359
recall_at_100 0.9249
```

nDCG@10 is the number to compare: BEIR publishes 0.665 for BM25 on SciFact. Precision@10 looks low because 277 of the 300 queries have only one relevant document, so no ranking can score above 0.1130 (`max_precision_at_10`).

Other scorers:

```bash
./build/search evaluate --scorer bm25f corpus/beir/scifact.idx corpus/beir/scifact/queries.jsonl corpus/beir/scifact/qrels/test.tsv
```

This gives `ndcg_at_10 0.6925`. `--scorer tfidf` gives the baseline.

TREC-COVID works the same way: replace `scifact` with `trec-covid` in every command above. It is 74 MB and 171,331 documents, and scores 0.6632 with BM25 and 0.6853 with BM25F, against a published 0.656.

## Step 10. Crawl the web (optional)

This step needs libcurl.

```bash
./build/search crawl https://example.com --max-pages 100 --out corpus/crawled
```

```bash
./build/search index-write corpus/crawled crawled.idx
```

The crawler obeys `robots.txt`, limits its request rate per host, and skips pages whose content it has already seen. Crawl your own sites, or ones whose terms allow it. [COMMANDS.md](COMMANDS.md) shows PageRank over a crawl.

---

## Troubleshooting

### Build errors

**`fatal error: 'algorithm' file not found`** (or `<filesystem>`, `<vector>`, or any other standard header), on macOS. The Command Line Tools install is incomplete. Build with Homebrew's compiler instead, starting from a clean `build` folder:

```bash
brew install gcc
```

```bash
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(ls /opt/homebrew/bin/g++-[0-9]* | sort -V | tail -1)
```

```bash
cmake --build build -j
```

**Undefined `std::__cxx11` symbols when linking**, on macOS. `CPLUS_INCLUDE_PATH` is set in your shell, so the compiler reads one standard library's headers and links another. Build without it:

```bash
env -u CPLUS_INCLUDE_PATH cmake --build build -j
```

**`CMake 3.20 or higher is required`.** Your CMake is too old. Install a newer one with `brew install cmake`, or on Linux with `pip install cmake`.

### Running errors

**`bench build needs a corpus directory or a .corpus file`**, or **`not a corpus file`**, for a `.corpus` file that exists. The import in step 5 did not finish, so the file has no header. Delete it and run step 5 again:

```bash
rm corpus/wikipedia/simplewiki.corpus
```

```bash
./tools/fetch_wikipedia.sh simple --yes
```

**`not a directory`** or **`no such corpus`.** The path is wrong either you are not in the repository's top folder, or an earlier step did not finish. Run `ls CMakeLists.txt` to check the folder.


**No snippets, only document ids and scores.** The index cannot find the text. Put the corpus next to the index with the same name, or add `--corpus <corpus>`.

**The index build is killed, or the machine starts swapping.** Not enough memory. Use `index-build --block 5000` (step 6), or a smaller block.

**`this build cannot fetch over http`.** libcurl was not found when you built. Install it (step 1), then delete `build` and repeat step 3.