# Copyright (c) Meta Platforms, Inc. and affiliates.

import hashlib
import os
import random
import sys
import tempfile
import zipfile


def write_data_file(zf, filename, max_file_size):
    path = ""

    if random.random() < 0.5:
        path += "subdir/"

    if random.random() < 0.5:
        path += "data/"
    else:
        path += "xl_model_weights/"

    if random.random() < 0.5:
        path += "suffix/"

    path += filename
    data = random.randbytes(random.randint(0, max_file_size))
    zf.writestr(path, data)


def write_other_file(zf, filename, max_file_size):
    path = ""
    if random.random() < 0.5:
        path += "code/"
    path += filename
    data = random.randbytes(random.randint(0, max_file_size))
    zf.writestr(path, data)


def generate_zipfile(max_file_size):
    with tempfile.NamedTemporaryFile() as f:
        with zipfile.ZipFile(f.name, "w") as zf:
            num_files = random.randint(0, 20)
            for i in range(num_files):
                if random.random() < 0.5:
                    write_data_file(zf, str(i), max_file_size)
                else:
                    write_other_file(zf, str(i), max_file_size)

        with open(f.name, "rb") as f:
            return f.read()


def generate_corpus(num_files, max_file_size):
    for _ in range(num_files):
        yield generate_zipfile(max_file_size)


def main(test_suite, test_case, out_dir):
    if test_suite not in ("PytorchModelParserTest", "ZipLexerTest"):
        raise ValueError(f"Unknown test suite: {test_suite}")

    num_files = 100
    if test_suite == "ZipLexerTest":
        max_file_size = 100
    elif "Large" in test_case:
        max_file_size = 10000000
        num_files = 1
    else:
        max_file_size = 10000

    corpus = generate_corpus(num_files, max_file_size)

    os.makedirs(out_dir, exist_ok=True)
    for blob in corpus:
        sha = hashlib.sha256(blob).hexdigest()
        path = os.path.join(out_dir, sha)
        with open(path, "wb") as f:
            f.write(blob)


if __name__ == "__main__":
    main(*sys.argv[1:])
