"""`search beir-import` and `search evaluate` -- scoring against judgements someone else wrote."""

NAME = "Relevance measurement"
ORDER = 530

CASES = [
    {
        "name": "a BEIR corpus imports with its own document ids",
        "argv": ["beir-import", "tests/fixtures/beir/corpus.jsonl", "build/beir-test.corpus"],
        "stdout": "lines 4\ndocuments 3\nskipped 1",
    },
    {
        "name": "the ids are BEIR's, not ordinals, because the qrels name them",
        "argv": ["docs", "build/beir-test.corpus"],
        "stdout": "d1\nd10\nd2",
    },
    {
        "name": "title and body are both indexed",
        "argv": ["show", "build/beir-test.corpus", "d1"],
        "stdout": "id: d1\ntitle: Cats\n\nCats sit on mats.",
    },
    {
        "name": "a record with neither title nor text is skipped",
        "argv": ["match", "build/beir-test.corpus", "empty"],
        "stdout": "",
    },
    {
        # Query 1 judges d1 relevant and it ranks first, so every measure is
        # perfect. Query 2 judges d10, which no query term reaches, so it
        # scores zero. The average of the two is what the command prints.
        "name": "the measures average over judged queries",
        "argv": ["evaluate", "build/beir-test.corpus",
                 "tests/fixtures/beir/queries.jsonl", "tests/fixtures/beir/qrels.tsv"],
        "stdout": "queries 2\nunjudged 1\nscorer bm25\ncutoff 10\nndcg_at_10 0.5000\n"
                  "precision_at_10 0.0500\nmax_precision_at_10 0.1000\n"
                  "recall_at_10 0.5000\nf1_at_10 0.0909\nmrr_at_10 0.5000\n"
                  "map 0.5000\nr_precision 0.5000\nrecall_at_100 0.5000",
    },
    {
        # Precision at 10 cannot exceed 0.1 when a query has one relevant
        # document, which is why max_precision_at_10 is reported beside it.
        # At a cutoff of 1 the ceiling is 1.0, and the measure is then just
        # "was the top result relevant": yes for q1, no for q2.
        "name": "the cutoff moves every at_k measure and its ceiling",
        "argv": ["evaluate", "--cutoff", "1", "build/beir-test.corpus",
                 "tests/fixtures/beir/queries.jsonl", "tests/fixtures/beir/qrels.tsv"],
        "stdout": "queries 2\nunjudged 1\nscorer bm25\ncutoff 1\nndcg_at_1 0.5000\n"
                  "precision_at_1 0.5000\nmax_precision_at_1 1.0000\n"
                  "recall_at_1 0.5000\nf1_at_1 0.5000\nmrr_at_1 0.5000\n"
                  "map 0.5000\nr_precision 0.5000\nrecall_at_100 0.5000",
    },
    {
        # MAP and R-precision are cutoff independent: both are defined by the
        # number of relevant documents, not by k.
        "name": "map and r-precision do not move with the cutoff",
        "argv": ["evaluate", "--cutoff", "5", "build/beir-test.corpus",
                 "tests/fixtures/beir/queries.jsonl", "tests/fixtures/beir/qrels.tsv"],
        "stdout": "queries 2\nunjudged 1\nscorer bm25\ncutoff 5\nndcg_at_5 0.5000\n"
                  "precision_at_5 0.1000\nmax_precision_at_5 0.2000\n"
                  "recall_at_5 0.5000\nf1_at_5 0.1667\nmrr_at_5 0.5000\n"
                  "map 0.5000\nr_precision 0.5000\nrecall_at_100 0.5000",
    },
    {
        "name": "a cutoff of zero is rejected",
        "argv": ["evaluate", "--cutoff", "0", "build/beir-test.corpus",
                 "tests/fixtures/beir/queries.jsonl", "tests/fixtures/beir/qrels.tsv"],
        "exit_code": 1,
        "stderr_not_empty": True,
    },
    {
        "name": "a non-numeric cutoff is rejected",
        "argv": ["evaluate", "--cutoff", "ten", "build/beir-test.corpus",
                 "tests/fixtures/beir/queries.jsonl", "tests/fixtures/beir/qrels.tsv"],
        "exit_code": 1,
        "stderr_not_empty": True,
    },
    {
        "name": "a missing qrels file is reported",
        "argv": ["evaluate", "build/beir-test.corpus",
                 "tests/fixtures/beir/queries.jsonl", "tests/fixtures/beir/nope.tsv"],
        "exit_code": 1,
        "stderr_not_empty": True,
    },
    {
        "name": "a missing queries file is reported",
        "argv": ["evaluate", "build/beir-test.corpus",
                 "tests/fixtures/beir/nope.jsonl", "tests/fixtures/beir/qrels.tsv"],
        "exit_code": 1,
        "stderr_not_empty": True,
    },
    {
        "name": "beir-import with no destination is rejected",
        "argv": ["beir-import", "tests/fixtures/beir/corpus.jsonl"],
        "exit_code": 2,
        "stderr_not_empty": True,
    },
]
