#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

# pyre-unsafe

import datetime
import logging
import os
import uuid
from enum import Enum
from typing import Optional

import click

from .phabricator_utils import PhabricatorUtils
from .zstrong_gbenchmarks import (
    ZstrongGoogleBenchmarkResults,
    ZstrongGoogleBenchmarkRunner,
)

logging.basicConfig(level=logging.INFO)
log = logging.getLogger("BenchmarkRunner")


DEV_LATEST = "dev_latest"


class Console(Enum):
    NO = 1
    DETAILED = 2
    AGGREGATED = 3
    COMPARE = 4


class SignalType(Enum):
    SUCCESS = "Success"
    REGRESSION = "Regression"
    FAILURE = "Failure"
    PENDING = "Pending"


class MainHandler:
    results: Optional[ZstrongGoogleBenchmarkResults] = None
    run_id: str = ""
    release: str = "Local"
    upload: bool = False
    console: Console = Console.NO
    compare: Optional[str] = None
    phab_version: Optional[int] = None
    start_time: Optional[datetime.datetime] = None
    end_time: Optional[datetime.datetime] = None

    def __init__(self):
        self.run_id = uuid.uuid4()

    def get_results(self) -> ZstrongGoogleBenchmarkResults:
        if not self.results:
            raise RuntimeError("MainHandler must have results")
        return self.results

    def report_start_time(self):
        self.start_time = datetime.datetime.now()
        self.log_to_phabricator(
            SignalType.PENDING,
            f"Started executing benchmark at {self.start_time.strftime('%m-%d-%Y %H:%M:%S')}.\n"
            f"Check on current run status using \n`buck2 run fbcode//zstrong/benchmark_service:client -- query {self.run_id}`.",
        )

    def log_to_phabricator(self, signal_type: SignalType, content: str) -> None:
        if self.phab_version is None:
            return
        assert isinstance(self.phab_version, int)

        severity = {
            SignalType.SUCCESS: PhabricatorUtils.SEVERITY.PASSED,
            SignalType.REGRESSION: PhabricatorUtils.SEVERITY.WARNING,
            SignalType.FAILURE: PhabricatorUtils.SEVERITY.FAILED,
            SignalType.PENDING: PhabricatorUtils.SEVERITY.WARNING,
        }[signal_type]

        title = f"Zstrong Benchmark - {signal_type.value}"

        phab_utils = PhabricatorUtils()
        log.info(f"Finding version fbid for Phabricator version {self.phab_version}")
        version_fbid = phab_utils.phabricator_version_phabricator_version_fbid(
            self.phab_version
        )
        log.info(f"Adding benchmark signal to {version_fbid}")
        phab_utils.add_signal(
            version_fbid, title, content, severity, deduplication_key="results"
        )

    def process_results(self):
        try:
            results = self.get_results()
            results.add_metadata({"run_id": self.run_id, "release": self.release})
            # If we compare to latest dev, fetch the run_id before uploading current results
            if self.compare == DEV_LATEST:
                self.compare = ZstrongGoogleBenchmarkResults.get_latest_dev_run_id()

            # Upload results to Scuba
            if self.upload:
                log.info("Uploading raw results to scuba")
                results.to_scuba(table="zstrong_benchmarks", subset="raw")
                log.info("Uploading aggregated results to scuba")
                results.aggregate().to_scuba(table="zstrong_benchmarks")
                log.info(
                    f"Done uploading results to scuba, you can find them with run_id={self.run_id} release={self.release}"
                )

            # Compare
            compared = None
            if self.compare:
                other = ZstrongGoogleBenchmarkResults.from_scuba(
                    table="zstrong_benchmarks",
                    subset="raw",
                    filters={"run_id": self.compare},
                )
                compared = results.compare(other)
                content = []
                if self.start_time and self.end_time:
                    content.append(
                        f"Benchmark ended at {self.end_time.strftime('%m-%d-%Y %H:%M:%S')} and took {(self.end_time - self.start_time).total_seconds() / 60:0.2f} minutes to execute."
                    )
                content.append(
                    f"Comparing current run {self.run_id} against {self.compare}"
                )
                content.append("\n" + compared.format(only_changed=True))

                # Upload to Phabricator
                self.log_to_phabricator(
                    (
                        SignalType.REGRESSION
                        if compared.is_regression()
                        else SignalType.SUCCESS
                    ),
                    "\n".join(content),
                )
            else:
                self.log_to_phabricator(
                    SignalType.FAILURE,
                    "Benchmark done, but no comparison was made.",
                )
        except Exception as e:
            log.error(e)
            self.log_to_phabricator(
                SignalType.FAILURE,
                f"Exception during benchmark processing: \n```\n{e}\n```\n",
            )
            raise e

        # Output requested result to console
        if self.console == Console.DETAILED:
            print(results.to_markdown())
        elif self.console == Console.AGGREGATED:
            print(results.aggregate().to_markdown())
        if self.console == Console.COMPARE:
            if compared:
                print(compared.format())
            else:
                log.error("Comparison result isn't availabe, can't output to console")

    def process_runner(self, runner: ZstrongGoogleBenchmarkRunner):
        try:
            self.report_start_time()
            self.results = runner.run()
            self.end_time = datetime.datetime.now()
            self.process_results()
        except Exception as e:
            log.error(e)
            self.log_to_phabricator(
                SignalType.FAILURE,
                f"Exception while running benchmark: \n```\n{e}\n```\n",
            )
            raise e

    def process_file(self, file):
        self.results = ZstrongGoogleBenchmarkResults.from_file(file)
        self.process_results()

    def process_scuba(self):
        if self.upload:
            self.upload = False
            log.info("Disabling upload for results brought from scuba")
        self.results = ZstrongGoogleBenchmarkResults.from_scuba(
            table="zstrong_benchmarks", subset="raw", filters={"run_id": self.run_id}
        )
        self.process_results()


@click.group(help="CLI tool run and handle benchmark results")
@click.option(
    "--run-id",
    default=None,
    type=str,
    help="ID of the run, if none given a new id will be auto generated",
)
@click.option(
    "--release",
    default="Local",
    type=click.Choice(["dev", "release", "local", "diff"], case_sensitive=False),
    help="The branch of the code originating this benchmark (default: Local)",
)
@click.option(
    "--upload/--no-upload",
    default=False,
    help="Should upload results to scuba (default: False)",
)
@click.option(
    "--console",
    type=click.Choice(
        [i.lower() for i in Console.__members__.keys()], case_sensitive=False
    ),
    default="no",
    help="Should print to console (default: no)",
)
@click.option(
    "--compare",
    type=str,
    default=DEV_LATEST,
    help=f"Run-id to compare to, {DEV_LATEST} can be used to signal latest dev run (default: {DEV_LATEST})",
)
@click.option(
    "--phab_version",
    type=int,
    default=None,
    help="If given will add comparison as a signal to this phabricator version",
)
@click.pass_context
def cli(ctx, run_id, release, upload, console, compare, phab_version):
    handler: MainHandler = ctx.obj
    if run_id:
        handler.run_id = run_id
    handler.release = release
    handler.upload = upload
    handler.console = Console[console.upper()]
    handler.compare = compare
    handler.phab_version = phab_version


@cli.command()
@click.option(
    "--binary",
    type=click.Path(exists=True, resolve_path=True),
    default="gbench",
    help="Path to benchmark binary (default: gbench)",
)
@click.option(
    "--repetitions",
    type=int,
    default=10,
    help="Number of repetitions for each test (default: 10)",
)
@click.option("--filter", type=str, default="", help="Filter benchmarks by regex")
@click.option(
    "--min-time",
    type=float,
    default=None,
    help="Minimum time for each test case",
)
@click.option(
    "--cpu",
    type=int,
    default=lambda: os.environ.get("BENCH_CPU", ""),
    help="When given benchmark will run on the specified CPU",
)
@click.option(
    "--short/--no-short",
    type=bool,
    default=None,
    help="When given benchmark will run only short list of tests, if not given short mode will be used for for all releases apart from dev/release.",
)
@click.argument("binary_arguments", type=str, nargs=-1)
@click.pass_context
def execute(ctx, binary, repetitions, min_time, filter, cpu, short, binary_arguments):
    handler: MainHandler = ctx.obj
    if short is None:
        short = handler.release not in ["dev", "release"]
    handler.process_runner(
        ZstrongGoogleBenchmarkRunner(
            binary,
            repetitions=repetitions,
            min_time=min_time,
            filter=filter,
            cpu=cpu,
            short=short,
            additional_args=binary_arguments,
        )
    )


@cli.command()
@click.argument("file", type=click.File("rb"))
@click.pass_context
def process(ctx, file):
    handler: MainHandler = ctx.obj
    handler.process_file(file)


@cli.command()
@click.pass_context
def process_scuba(ctx):
    handler: MainHandler = ctx.obj
    handler.process_scuba()


def main() -> None:
    cli(obj=MainHandler(), auto_envvar_prefix="BENCH")


if __name__ == "__main__":
    main()
