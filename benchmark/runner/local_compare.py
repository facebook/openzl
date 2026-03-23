# Copyright (c) Meta Platforms, Inc. and affiliates.

# pyre-unsafe
import datetime
import hashlib
import logging
import os
import re
import socket
import subprocess
import typing as t

import click
from scm.hgrepo import HgCommit, HgRepository

from .phabricator_utils import PHAB_ACCESS_TYPE, PhabricatorUtils
from .quiet_cpu_utils import QuietCPUManager
from .zstrong_gbenchmarks import (
    ZstrongGoogleBenchmarkResults,
    ZstrongGoogleBenchmarkRunner,
)

logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)
log = logging.getLogger("BenchmarkRunner")


DEAFULT_BUCK_TAGET = "//openzl/dev/benchmark:benchmark"
DEFAULT_REPETITIONS = 10
DEFAULT_MIN_TIME = 0.1


def get_repo() -> HgRepository:
    return HgRepository(os.getcwd())


def get_commit(repo: HgRepository, commit_hash: str) -> HgCommit:
    if commit_hash == "master":
        commit_hash = "ancestor(., master)"
    elif not commit_hash:
        commit_hash = "."
    commit = repo.get_commit(commit_hash)
    if commit is None:
        raise RuntimeError(f"Commit {commit_hash} not found")
    return commit


def normalize_commit_hash(repo: HgRepository, commit_hash: str) -> str:
    return get_commit(repo, commit_hash).hash


def get_ancestor_hash(repo: HgRepository, rev=None, back=0) -> str:
    return repo.get_ancestors(rev, limit=back + 1)[back].hash


def get_commit_diff(repo: HgRepository, commit_hash: str) -> str:
    return get_commit(repo, commit_hash).phabdiff()


class ScopedRepoRevision:
    def __init__(
        self,
        repo: t.Optional[HgRepository] = None,
        commit_hash: t.Optional[str] = None,
        shelve: bool = False,
    ):
        if repo is None:
            repo = get_repo()
        self.repo = repo
        self.commit_hash = commit_hash
        self.shelve = shelve
        self.shelved = False
        self.prev_commit_hash = None

    def __enter__(self):
        if self.repo.is_dirty() and self.commit_hash:
            if self.shelve:
                backup_filename = (
                    f"/tmp/backup-{datetime.datetime.now().isoformat()}.diff"
                )
                log.info(f"Backing up dirty working set diff to {backup_filename}")
                with open(backup_filename, "wt") as bu:
                    bu.write(self.repo.get_working_copy().diff())
                log.info("Shelving changes")
                self.repo.shelve()
                self.shelved = True
            else:
                raise RuntimeError(
                    "Working set is dirty, please commit your changes first or use --shelve"
                )
        current_commit_hash = get_ancestor_hash(self.repo)
        if self.commit_hash and current_commit_hash != normalize_commit_hash(
            self.repo, self.commit_hash
        ):
            self.prev_commit_hash = current_commit_hash
            log.info(
                f"Moving from commit {self.prev_commit_hash} to commit {self.commit_hash}"
            )
            self.repo.checkout(self.commit_hash)

    def __exit__(self, type, value, traceback):
        if self.prev_commit_hash:
            log.info(f"Moving back to commit {self.prev_commit_hash}")
            self.repo.checkout(self.prev_commit_hash)
        if self.shelved:
            log.info("Unshelving changes")
            self.repo.unshelve()


def generate_run_id(buck_target: str, revision_data: str, *args, **kwargs) -> str:
    data = "".join(
        [
            buck_target,
            revision_data,
            socket.gethostname(),
            repr(args),
            repr(kwargs),
        ]
    )
    return "local-" + hashlib.md5(data.encode()).hexdigest()


def generate_run_id_working_set(buck_target: str, *args, **kwargs) -> str:
    repo = get_repo()
    if repo.is_dirty():
        diff = repo.get_working_copy().diff()
    else:
        diff = ""
    commit_hash = get_ancestor_hash(repo)
    return generate_run_id(buck_target, commit_hash + diff, *args, **kwargs)


def get_turbo_status() -> bool:
    status = subprocess.run(
        args=[
            "sudo",
            "/usr/local/fbprojects/dynamoserver/bin/turboDriver",
            "status",
        ],
        capture_output=True,
        check=True,
    )
    if b"Turbo is off" in status.stdout:
        return False
    if b"Turbo is on" in status.stdout:
        return True
    raise RuntimeError(
        f"Unable to parse turbo status!\nstderr: '{status.stderr}'\nstdout: '{status.stdout}'"
    )


def build_and_bench(
    buck_target: str, cpu: t.Optional[int], **kwargs
) -> ZstrongGoogleBenchmarkResults:
    cmd = f"buck2 build @//mode/opt -c cxx.use_default_autofdo_profile=false {buck_target} --show-full-simple-output"
    log.info(f"Building benchmark binary using: {cmd}")
    build_res = subprocess.run(
        cmd,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if build_res.returncode:
        raise RuntimeError(
            f"Failed to build {buck_target}: {build_res.stderr.decode()}"
        )
    binary_path = str(build_res.stdout, encoding="utf-8").strip()
    if get_turbo_status():
        raise RuntimeError(
            "Turbo is enabled, cannot benchmark, please disable with `~/fbsource/fbcode/data_compression/scripts/benchmark.py enable`."
        )
    if cpu is None:
        cpu = QuietCPUManager().get_available_cpu()
    log.info(f"Finished building binary, now running on core {cpu}")
    ret = ZstrongGoogleBenchmarkRunner(
        binary_path, cpu=cpu, fbcode=True, **kwargs
    ).run()
    if get_turbo_status():
        raise RuntimeError("Turbo was enabled sometimes during benchmarking.")
    return ret


def bench(
    run_id: str, buck_target: str, cpu: t.Optional[int], **kwargs
) -> ZstrongGoogleBenchmarkResults:
    log.info(f"Starting benchmark for {run_id}")
    res = build_and_bench(buck_target, cpu, **kwargs)
    log.info(f"Finished benchmark for {run_id}")
    res.add_metadata(
        {"run_id": run_id, "release": "local", "hostname": socket.gethostname()}
    )
    return res


def bench_revision(
    commit: str,
    shelve: bool,
    cache: bool,
    short: bool,
    repetitions: int,
    min_time: float,
    buck_target: str,
    filter: str,
    cpu: t.Optional[int],
    additional_args: t.List[str],
) -> ZstrongGoogleBenchmarkResults:
    if commit == "":
        run_id = generate_run_id_working_set(
            buck_target,
            short=short,
            repetitions=repetitions,
            filter=filter,
            additional_args=sorted(additional_args),
        )
    else:
        run_id = generate_run_id(
            buck_target,
            normalize_commit_hash(get_repo(), commit),
            short=short,
            repetitions=repetitions,
            min_time=min_time,
            filter=filter,
            additional_args=sorted(additional_args),
        )
    results = None
    if cache:
        try:
            log.info(f"Querying Scuba for results for run-id={run_id}")
            results = ZstrongGoogleBenchmarkResults.from_scuba(
                filters={"run_id": run_id}, only_latest=True, subset="raw"
            )
            log.info(f"Fetched benchmark results for run-id={run_id} from scuba")
        except KeyboardInterrupt:
            raise
        except Exception:
            pass
    if results is None:
        log.info(f"Running benchmark for run-id={run_id}")
        with ScopedRepoRevision(commit_hash=commit, shelve=shelve):
            results = bench(
                run_id,
                buck_target,
                cpu=cpu,
                short=short,
                repetitions=repetitions,
                min_time=min_time,
                filter=filter,
                additional_args=additional_args,
            )
            upload_results_to_scuba(results)
    return results


def upload_results_to_scuba(results: ZstrongGoogleBenchmarkResults):
    log.info("Uploading raw results to scuba")
    results.to_scuba(table="zstrong_benchmarks", subset="raw")
    log.info("Uploading aggregated results to scuba")
    results.aggregate().to_scuba(table="zstrong_benchmarks")


def update_diff_test_plan(compare_results: str, commit_hash: str) -> None:
    diff_id = get_commit_diff(get_repo(), commit_hash)
    if not diff_id:
        log.error("Couldn't fidn a diff, can't update test_plan!")
        return

    phab = PhabricatorUtils(access_type=PHAB_ACCESS_TYPE.WRITE_USER)
    log.info("Reading current test plan from Phabricator.")
    current_test_plan = phab.phabricator_get_test_plan(diff_id)
    test_plan_block = f"Zstrong local benchmark results:\n```\n{compare_results}\n```"
    if "Zstrong local benchmark results" in current_test_plan:
        log.info(f"Found existing test plan for {diff_id} in Phabricator, updating.")
        new_test_plan = re.sub(
            r"Zstrong local benchmark results:.```[^`]*```",
            test_plan_block,
            current_test_plan,
            flags=re.MULTILINE | re.DOTALL,
        )
    else:
        new_test_plan = current_test_plan + "\n\n" + test_plan_block
    log.info(f"Writing new test plan for {diff_id} to Phabricator.")
    phab.phabricator_update_test_plan(diff_id, new_test_plan)


@click.command()
@click.option(
    "--buck-target",
    type=str,
    default=DEAFULT_BUCK_TAGET,
    help="Path to buck target of Zstrong Google Benchmak binary",
)
@click.option(
    "--commit",
    type=str,
    default="",
    help="Commit hash to benchmark, empty value means current working set, '.' means current commit (default: current working set)",
)
@click.option(
    "--cache/--no-cache",
    type=bool,
    default=True,
    help="Use caching for benchmark results (default: True)",
)
@click.option(
    "--shelve/--no-shelve",
    type=bool,
    default=False,
    help="Allow working on an unclean working set by shelving changes, a backup diff should be generated to /tmp/backup.diff in case something goes wrong (default: False)",
)
@click.option(
    "--update-diff/--no-update-diff",
    type=bool,
    default=False,
    help="Update the revision's diff's test plan with the results, can only work on clean working set (default: off)",
)
@click.option(
    "--changes-only/--no-changes-only",
    type=bool,
    default=True,
    help="Only include changes in results (default: on)",
)
@click.option(
    "--short/--no-short",
    type=bool,
    default=None,
    help="Set benchmarks to run in short mode (default: on if no filter is provided, otherwise off)",
)
@click.option(
    "--repetitions",
    type=int,
    default=DEFAULT_REPETITIONS,
    help=f"Number of repetitions for each benchmark (default {DEFAULT_REPETITIONS})",
)
@click.option(
    "--min-time",
    type=float,
    default=DEFAULT_MIN_TIME,
    help=f"Min time to spend on each repetitions (default {DEFAULT_MIN_TIME})",
)
@click.option(
    "--baseline-buck-target",
    type=str,
    default="",
    help="Path to buck target of baseline binary (default: same as --buck_target)",
)
@click.option(
    "--baseline-commit",
    type=str,
    default="",
    help="Commit hash of baseline binary (default: previous commit, use '.' for current commit and 'master' for the base merged commit)",
)
@click.option(
    "--baseline-repetitions",
    type=int,
    default=0,
    help="Number of repetitions for each baseline benchmark (default: same as --repetitions)",
)
@click.option(
    "--cpu",
    type=int,
    default=None,
    help="When given benchmark will run on the specified CPU, otherwise it will choose a core automatically",
)
@click.option(
    "--filter",
    type=str,
    default=None,
    help="Filter benchmarks by the provided regex (default: no filter)",
)
@click.argument("additional_args", type=str, nargs=-1)
def compare_versions(
    buck_target: str,
    commit: str = "",
    cache: bool = True,
    shelve: bool = False,
    update_diff: bool = False,
    changes_only: bool = True,
    short: t.Optional[bool] = None,
    repetitions: int = DEFAULT_REPETITIONS,
    min_time: float = DEFAULT_MIN_TIME,
    baseline_buck_target: str = "",
    baseline_commit: str = "",
    baseline_repetitions: int = 0,
    cpu: t.Optional[int] = None,
    filter: str = "",
    additional_args: t.Iterable[str] = (),
) -> None:
    if not baseline_buck_target:
        baseline_buck_target = buck_target
    if not baseline_commit:
        baseline_commit = get_ancestor_hash(get_repo(), commit, 1)
    if baseline_repetitions == 0:
        baseline_repetitions = repetitions

    if short is None:
        short = filter is None

    if get_repo().is_dirty() and update_diff:
        log.error("Can't update a diff while there are unstaged changes.")
        return

    additional_args = list(additional_args)

    current_results = bench_revision(
        commit,
        shelve,
        cache,
        buck_target=buck_target,
        cpu=cpu,
        short=short,
        repetitions=repetitions,
        min_time=min_time,
        filter=filter,
        additional_args=additional_args,
    )

    baseline_results = bench_revision(
        baseline_commit,
        shelve,
        cache,
        buck_target=baseline_buck_target,
        cpu=cpu,
        short=short,
        repetitions=baseline_repetitions,
        min_time=min_time,
        filter=filter,
        additional_args=additional_args,
    )

    compare = current_results.compare(baseline_results)
    formatted = compare.format(only_changed=changes_only)
    log.info("Benchmark comparison results:\n" + formatted)

    if update_diff:
        short_arg = ""
        if short and filter:
            short_arg = "--short"
        elif not short and not filter:
            short_arg = "--no-short"
        replication_args = [
            (
                f'--buck-target="{buck_target}"'
                if buck_target != DEAFULT_BUCK_TAGET
                else ""
            ),
            f'--commit="{normalize_commit_hash(get_repo(), commit)}"',
            (
                f"--repetitions={repetitions}"
                if repetitions != DEFAULT_REPETITIONS
                else ""
            ),
            f"--min-time={min_time}" if min_time != DEFAULT_MIN_TIME else "",
            (
                f'--baseline-buck-target="{baseline_buck_target}'
                if baseline_buck_target != buck_target
                else ""
            ),
            f'--baseline-commit="{normalize_commit_hash(get_repo(), baseline_commit)}"',
            (
                f"--baseline-repetitions={baseline_repetitions}"
                if baseline_repetitions != repetitions
                else ""
            ),
            f'--filter "{filter}"' if filter else "",
            short_arg,
            "--" if additional_args else "",
            *additional_args,
        ]
        replication_args = " ".join([a for a in replication_args if a])
        replication_command_line = f"$ buck2 run //openzl/dev/benchmark/runner:local_compare -- {replication_args} \n\n"
        update_diff_test_plan(replication_command_line + formatted, commit)


if __name__ == "__main__":
    compare_versions()
