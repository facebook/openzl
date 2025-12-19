# Copyright (c) Meta Platforms, Inc. and affiliates.

# pyre-unsafe

import datetime
import json
import logging
import math
import os
import subprocess
from collections import namedtuple
from typing import Dict, IO, List, Optional, Tuple

import numpy as np
import pandas as pd
import scipy.stats as st

from .scuba_utils import ScubaUtils

log = logging.getLogger("BenchmarkRunner")

KeyMetric = namedtuple("KeyMetric", ["threshold", "higher_better", "deterministic"])


def base_dimensions() -> List[str]:
    return ["type", "name", "corpus", "op"]


def base_metrics() -> List[str]:
    return ["Size", "CompressedSize", "CompressionRatio", "Mbps"]


def key_metrics() -> Dict[str, KeyMetric]:
    return {
        "CompressionRatio": KeyMetric(
            threshold=0.01, higher_better=True, deterministic=True
        ),
        "Mbps": KeyMetric(threshold=0.02, higher_better=True, deterministic=False),
    }


class ZstrongComparedResults:
    def __init__(self, comp: pd.DataFrame):
        self.comp = comp

    def is_regression(self):
        return len(set(self.comp["change"].unique()) & {"Regressed"}) > 0

    def is_changed(self):
        return np.any(self.comp.changed.values)

    def format(self, only_changed=False):
        comp = self.comp.copy()
        if only_changed:
            comp = comp[self.comp["changed"]].copy()
            if len(comp) == 0:
                return "No changes detected!"

        columns = base_dimensions()
        for metric, descriptor in key_metrics().items():

            def format_row(row, conf=False):
                def format_metric_change(value, change=None, p_value=None):
                    change_str = (
                        f"({change:+0.2f}%)"
                        if change is not None and not math.isnan(change)
                        else ""
                    )
                    p_value_str = (
                        f"[p = {p_value:0.3f}]"
                        if p_value is not None and not math.isnan(p_value)
                        else ""
                    )
                    return f"{value:0.2f}{change_str}{p_value_str}"

                mean = row[f"{metric}_1"]
                mean_change = 100 * row[f"{metric}_diff"]
                if descriptor.deterministic:
                    return format_metric_change(mean, mean_change)

                low = row[f"{metric}_conf_low_1"]
                high = row[f"{metric}_conf_high_1"]
                low2 = row[f"{metric}_conf_low_2"]
                high2 = row[f"{metric}_conf_high_2"]
                p_value = row[f"{metric}_p_value_two_sided"]
                if conf:
                    low_change = 100 * (low - low2) / low2
                    high_change = 100 * (high - high2) / high2
                    return f"{format_metric_change(low, low_change)} .. {format_metric_change(high, high_change)}"
                return format_metric_change(mean, mean_change, p_value)

            comp[metric] = comp.apply(format_row, axis=1)
            columns.append(metric)

            if not descriptor.deterministic:
                comp[f"{metric} conf window"] = comp.apply(
                    lambda r: format_row(r, conf=True), axis=1
                )
                columns.append(f"{metric} conf window")

        comp = comp.sort_values(base_dimensions())

        result = []
        for change_type in ["Regressed", "Improved", "Added", "Removed", ""]:
            title = change_type if change_type else "Unchanged"
            current_comp = comp[comp.change == change_type][columns]
            if len(current_comp) == 0:
                continue
            result += [f"## {title}"]
            result += [
                current_comp.to_markdown(index=False)
                .replace("|:--", "|---")
                .replace("--:|", "---|"),
                "\n",
            ]

        return "\n".join(result)


class ZstrongGoogleBenchmarkResults:
    def __init__(
        self, results: pd.DataFrame, metadata: Optional[Dict[str, str]] = None
    ) -> None:
        if set(results.index.names) == set(base_dimensions()):
            self.results = results.copy().reset_index()
        elif set(results.columns) & set(base_dimensions()) == set(base_dimensions()):
            self.results = results.copy()
        else:
            raise RuntimeError(
                f"Missing dimension columns in resultset {set(base_dimensions()) - set(results.columns)}"
            )
        if ScubaUtils.TIME_COLUMN not in self.results.columns:
            self.set_timestamp()
        self.metadata = {}
        if metadata:
            self.add_metadata(metadata)
        results = results.sort_values(base_dimensions()).reset_index(drop=True)

    def dimensions(self):
        return [
            col
            for col in self.results.columns
            if all(not str(col).startswith(m) for m in base_metrics())
        ]

    def add_metadata(self, metadata: Dict[str, str]):
        for k, v in metadata.items():
            self.results[k] = v

    def set_timestamp(self, timestamp: Optional[float] = None):
        if timestamp is None:
            timestamp = datetime.datetime.now(datetime.timezone.utc).timestamp()
        self.results[ScubaUtils.TIME_COLUMN] = timestamp

    @classmethod
    def from_dict(cls, results: Dict):
        df = pd.DataFrame(results["benchmarks"])

        # In the case of multiple repetitions we have rows with aggregated data, not interested in those
        df = df[df["run_type"] == "iteration"].copy()

        # Reformat metrics
        df["Mbps"] = df["bytes_per_second"] / (1024**2)
        del df["bytes_per_second"]

        # Expand the testcase name into dimensions and set them
        df[base_dimensions()] = df.name.str.split(
            " / ", expand=True, n=len(base_dimensions())
        )
        df[base_dimensions()].fillna("N/A")
        for m in base_metrics():
            if m not in df:
                df[m] = np.NaN
        df = df[base_dimensions() + base_metrics()].copy()
        return cls(df)

    @classmethod
    def from_json(cls, results: str):
        return cls.from_dict(json.loads(results))

    @classmethod
    def from_file(cls, file: IO):
        return cls.from_dict(json.load(file))

    @classmethod
    def from_scuba(
        cls,
        table="zstrong_benchmarks",
        subset: Optional[str] = None,
        filters: Optional[Dict] = None,
        only_latest: bool = True,
    ):
        query_results = ScubaUtils.query_scuba(
            table=table, subset=subset, filters=filters, order_by=ScubaUtils.TIME_COLUMN
        )
        if any(query_results.columns.duplicated()):
            raise RuntimeError(
                f"Scuba table {table}/{subset} has duplciated columns, see https://www.internalfb.com/intern/qa/4589/how-to-solve-scuba-warning-duplicate-backend-colum"
            )
        for metric in base_metrics():
            query_results[metric] = query_results[metric].replace("null", np.NaN)
            query_results[metric] = query_results[metric].astype(float)
        if len(query_results) == 0:
            raise RuntimeError("Couldn't find results in scuba")
        if only_latest:
            time_col = query_results[ScubaUtils.TIME_COLUMN].astype(float)
            query_results = query_results[(max(time_col) - time_col) < 0.1].reset_index(
                drop=True
            )
            query_results = pd.DataFrame(query_results)
        return cls(query_results)

    @classmethod
    def get_latest_dev_run_id(cls) -> int:
        run_id = ScubaUtils.query_scuba(
            "zstrong_benchmarks",
            "raw",
            ["run_id", "time"],
            {"release": "dev"},
            order_by="time",
            limit=1,
        ).run_id[0]
        return run_id

    @classmethod
    def from_scuba_latest_dev(cls):
        run_id = cls.get_latest_dev_run_id()
        return cls.from_scuba("zstrong_benchmarks", "raw", filters={"run_id": run_id})

    def to_markdown(self) -> str:
        md = self.results.to_markdown(showindex=False).replace("|:--", "|---")
        if md is None:
            raise RuntimeError("Unable to create markdown, tabulate might be missing")
        return md

    def to_dataframe(self) -> pd.DataFrame:
        return self.results

    def to_scuba(self, table: str = "zstrong_benchmarks", subset: Optional[str] = None):
        # make sure all metric columns are numeric
        if any(self.results[m].dtype == object for m in key_metrics()):
            raise RuntimeError("Can't upload to scuba non numeric columns for metrics")
        ScubaUtils.upload_data_to_scuba(
            self.results, "zstrong_benchmarks", subset=subset
        )

    def aggregate(self, array=False):
        def add_confidence_intervals(df, metric):
            def get_confidence_interval(x):
                assert len(x[0]) >= 1
                data = x[0]
                if np.all(data == data[0]):
                    return pd.Series([data[0], data[0]], dtype=float)
                bs = st.bootstrap(x, np.mean)
                return pd.Series(
                    [bs.confidence_interval.low, bs.confidence_interval.high],
                    dtype=float,
                )

            df[[metric + "_conf_low", metric + "_conf_high"]] = df[[metric]].apply(
                get_confidence_interval, axis=1
            )

        grouped = []
        for metric, desc in key_metrics().items():
            log.debug(f"Processing metric {metric} {desc}")
            if metric not in self.results:
                self.results[metric] = np.NaN
                continue
            self.results[metric].fillna(np.NaN)

            def top_values(arr, n):
                if desc.deterministic or n == 0:
                    return arr
                return arr[np.argsort(arr)[n:-n]].copy()

            g = (
                self.results.groupby(self.dimensions())[metric]
                .apply(
                    lambda x: top_values(np.array(x, dtype=float), int(len(x) * 0.1))
                )
                .reset_index()
            )
            if desc.deterministic:
                # Check that all values are the same
                if not np.all(
                    self.results.groupby(self.dimensions())[metric]
                    .nunique(dropna=False)
                    .eq(1)
                ):
                    raise RuntimeError(
                        f"Metric {metric} is expected to be deterministic but isn't"
                    )
            else:
                add_confidence_intervals(g, metric)
            if array:
                g[metric + "_arr"] = g[metric]
            g[metric] = g[metric].apply(np.mean, axis=0)
            g = g.set_index(self.dimensions())
            grouped.append(g)

        result = pd.concat(grouped, axis=1).reset_index()
        return ZstrongGoogleBenchmarkResults(result)

    def compare(self, other, removed=False) -> ZstrongComparedResults:
        agg1 = self.aggregate(array=True).results.copy()
        agg2 = other.aggregate(array=True).results.copy()

        joint = agg1.merge(
            agg2, on=base_dimensions(), how="outer", suffixes=("_1", "_2")
        )
        joint["better"] = False
        joint["worse"] = False
        joint["added"] = False
        joint["removed"] = False

        metric_columns = []
        for metric, descriptor in key_metrics().items():
            for i in range(1, 3):
                metric_columns.append(f"{metric}_{i}")
                if not descriptor.deterministic:
                    metric_columns.append(f"{metric}_conf_low_{i}")
                    metric_columns.append(f"{metric}_conf_high_{i}")

            metric_columns.append(f"{metric}_diff")

            def calc_p_value(type="two-sided", shift=0):
                col_name = f"{metric}_p_value_{type.replace('-', '_')}"
                joint[col_name] = joint[[f"{metric}_arr_1", f"{metric}_arr_2"]].apply(
                    lambda x: st.ttest_ind(
                        x[0] * (1 + shift),
                        x[1],
                        trim=0.1,
                        equal_var=False,
                        alternative=type,
                        random_state=1337,
                    ).pvalue,
                    axis=1,
                )
                metric_columns.append(col_name)

            calc_p_value()
            calc_p_value("less", descriptor.threshold)
            calc_p_value("greater", -descriptor.threshold)

            joint[f"{metric}_diff"] = (
                joint[f"{metric}_1"] - joint[f"{metric}_2"]
            ) / joint[f"{metric}_2"]

            if descriptor.higher_better:
                increase_label = "better"
                decrease_label = "worse"
            else:
                increase_label = "worse"
                decrease_label = "better"
            joint[increase_label] |= (
                (joint[f"{metric}_p_value_two_sided"].fillna(0) < 0.05)
                & (joint[f"{metric}_p_value_greater"].fillna(0) < 0.05)
                & (joint[f"{metric}_diff"] > descriptor.threshold)
            )
            joint[decrease_label] |= (
                (joint[f"{metric}_p_value_two_sided"].fillna(0) < 0.05)
                & (joint[f"{metric}_p_value_less"].fillna(0) < 0.05)
                & (joint[f"{metric}_diff"] < -descriptor.threshold)
            )
            joint["added"] |= joint[f"{metric}_2"].isna() & ~joint[f"{metric}_1"].isna()
            if removed:
                joint["removed"] |= (
                    joint[f"{metric}_1"].isna() & ~joint[f"{metric}_2"].isna()
                )

        joint["change"] = ""
        joint.loc[joint.better, "change"] = "Improved"
        joint.loc[joint.worse, "change"] = "Regressed"
        joint.loc[joint.added, "change"] = "Added"
        joint.loc[joint.removed, "change"] = "Removed"
        joint["changed"] = ~(joint["change"] == "")

        return ZstrongComparedResults(
            joint[base_dimensions() + metric_columns + ["changed", "change"]]
        )

    def append(self, other):
        return ZstrongGoogleBenchmarkResults(pd.concat([self.results, other.results]))


class ZstrongGoogleBenchmarkRunner:
    MAX_REPS_PER_EXECUTION = 10

    def __init__(
        self,
        path: str,
        repetitions: int = 10,
        min_time: Optional[float] = 0,
        filter: Optional[str] = None,
        cpu: Optional[int] = None,
        short: bool = False,
        fbcode: bool = False,
        additional_args: Optional[List[str]] = None,
    ) -> None:
        self.path = path
        self.repetitions = repetitions
        self.min_time = min_time
        self.filter = filter
        self.cpu = cpu
        self.short = short
        self.fbcode = fbcode
        self.additional_args = additional_args

    def _run_process(self, path: str, args: List[str]) -> Tuple[int, str, str]:
        args = [path] + args
        if self.cpu is not None:
            taskset = f"taskset -c {self.cpu}".split(" ")
            args = taskset + args
        env = os.environ.copy()
        if not self.fbcode:
            env.setdefault(
                "BENCH_CORPUS_PATH",
                os.path.normpath(os.path.join(path, "../../corpus")),
            )
        log.info(f"Executing {' '.join(args)}")
        process = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        stdout = process.stdout.decode()
        stderr = process.stderr.decode()
        exit_code = process.returncode
        return (exit_code, stdout, stderr)

    def run(self) -> ZstrongGoogleBenchmarkResults:
        result = None
        for rep in range(0, self.repetitions, self.MAX_REPS_PER_EXECUTION):
            args = [
                "--benchmark_format=json",
                # "--benchmark_enable_random_interleaving=true",
                f"--benchmark_repetitions={min(self.MAX_REPS_PER_EXECUTION, self.repetitions - rep)}",
            ]
            if self.min_time:
                args.append(f"--benchmark_min_time={self.min_time}")
            if self.filter:
                args.append(f"--benchmark_filter={self.filter}")
            if self.short:
                args.append("--short")
            if self.additional_args:
                args += self.additional_args
            exit_code, stdout, stderr = self._run_process(
                self.path,
                args,
            )
            log.debug(f"exit_code: {exit_code} Stderr: {stderr}")
            if exit_code != 0 or not stdout:
                raise RuntimeError(
                    f"Benchmark process returned error code {exit_code}\n\nStderr: {stderr}\n\nStdout: {stdout}"
                )
            curr_result = ZstrongGoogleBenchmarkResults.from_json(stdout)
            if result:
                result = result.append(curr_result)
            else:
                result = curr_result
        result.set_timestamp()
        return result
