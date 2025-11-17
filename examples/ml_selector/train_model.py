#!/usr/bin/env fbpython
# Copyright (c) Meta Platforms, Inc. and affiliates.

import logging
import os
import typing as t

import click

import numpy as np
import pandas as pd
import xgboost as xgb
import zstrong_ml  # @manual=//data_compression/experimental/zstrong/tools/py:zstrong_ml
from sklearn.model_selection import train_test_split

# Ugly fix for old XGBoost version using deprecated Pandas features
setattr(pd, "Int64Index", pd.Index)
setattr(pd, "Float64Index", pd.Index)

logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


def train_model(path: str) -> t.Tuple[xgb.XGBClassifier, pd.DataFrame]:
    data = open(path, "rt").read()
    samples = zstrong_ml.samples_from_json(data)
    logger.info(f"Read {len(samples)} training samples")

    # def choice_function(sizes: Dict[str, float], ctimes: Dict[str, float]) -> str:
    #     best_label = min(sizes, key=lambda k: sizes[k])
    #     best_size = sizes[best_label]
    #     best_ctime = ctimes[best_label]
    #     possible = [k for k, v in sizes.items() if v <= best_size * 1.05]
    #     scores = {}
    #     for p in possible:
    #         scores[p] = (
    #             5 * (sizes[p] - best_size) / best_size
    #             - (best_ctime - ctimes[p]) / best_ctime
    #         )
    #     return min(possible, key=lambda k: scores[k])

    # Process the samples into dataframes
    df_features, df_results = zstrong_ml.process_training_samples(
        samples, None  # , choice_function
    )

    choice_breakdown = (
        pd.DataFrame(df_results["choice"].value_counts()).reset_index().to_string()
    )
    logger.info(f"Breakdown of successor choice for sample data:\n{choice_breakdown}")

    # Divide features into training and testing sets
    seed = 15
    test_size = 0.3
    X_train, X_test, y_train, y_test = train_test_split(
        df_features, df_results, test_size=test_size, random_state=seed
    )
    test_mask = y_test["choice"].isin(y_train["choice"].unique())
    X_test = X_test.loc[test_mask].copy()
    y_test = y_test.loc[test_mask].copy()

    params = {
        "learning_rate": 0.1,
        "n_estimators": 30,
        "n_jobs": 1,
        "min_child_weight": 0.0,
        "subsample": 0.7,
        "colsample_bynode": 0.8,
    }
    logger.info("Training model")
    booster = xgb.sklearn.XGBClassifier(**params)
    _ = booster.fit(
        X_train,
        y_train["choice"],
        eval_set=[(X_train, y_train["choice"]), (X_test, y_test["choice"])],
        # eval_metric="mae",
        early_stopping_rounds=10,
        verbose=False,
    )

    logger.info("Model trained")

    # pyre-ignore
    params["n_estimators"] = booster.best_iteration + 1
    booster = xgb.sklearn.XGBClassifier(**params)
    _ = booster.fit(
        df_features,
        df_results["choice"],
        verbose=False,
    )

    prediction = booster.predict(X_test)

    prediction_breakdown = (
        pd.DataFrame(pd.Series(prediction).value_counts()).reset_index().to_string()
    )
    logger.info(f"Breakdown of predictions for test data:\n{prediction_breakdown}")

    prediction_size = sum(
        [sum(y_test[p].values[prediction == p]) for p in np.unique(prediction)]
    )
    prediction_best_size = sum(y_test.best_size)
    prediction_ratio = prediction_size / prediction_best_size
    logger.info(
        f"Using our model on the training + test data we will get {100 * (prediction_ratio-1):.02}% worse compression ratio than optimum (predicted size = {prediction_size:.02}, best size = {prediction_best_size:.02})"
    )
    return (booster, df_features)


def get_trained_serialized_model(path: str):
    (booster, df_features) = train_model(path)
    # This is a fix for when the model makes a constant prediction.
    # Our GBT predictor expects there to be at least two classes,
    # so we add another even if there's only one class.
    # TODO: fix GBT predictors, consider using forced_successor
    # pyre-ignore
    classes = list(booster.classes_)
    if len(classes) == 1:
        classes.append(classes[0])

    return zstrong_ml.get_serialized_model(
        booster,
        list(df_features.columns),
        classes,
    )


def get_trained_core_model(path: str, prefix: str) -> zstrong_ml.CoreModel:
    # Get the trained model and return a dictionary representing the model

    (booster, df_features) = train_model(path)
    # Fix as mentioned in the get_trained_serialized_model
    classes = list(booster.classes_)
    if len(classes) == 1:
        classes.append(classes[0])

    return zstrong_ml.get_core_model(
        booster, list(df_features.columns), classes, prefix
    )


def write_core_model(path: str, model_strings: str):
    """
    Writes a .c file containing all the implementation details needed to
    initialize a GBTModel for a core model.
    """
    c_path = path + ".c"
    include_str = f'#include "{path}.h"' if path else ""

    output = f"""// Copyright (c) Meta Platforms, Inc. and affiliates.
// THIS FILE WAS AUTOMATICALLY GENERATED BY train_model.py

{include_str}

// clang-format off

{model_strings}
"""
    if path:
        c_path = path + ".c"
        with open(c_path, "wt") as f:
            f.write(output)
    else:
        print(output)


def write_core_files(path: str, model_strings: t.Dict[str, zstrong_ml.CoreModel]):
    """
    Writes a header file containing extern declarations that can be used to
    implement a core GBTModel
    """
    c_strings = ""
    header_strings = ""
    for K, V in model_strings.items():
        c_strings += f"// GENERATED {K} MODEL MEMBER FIELDS\n"
        c_strings += V.c_strings + "\n"
        header_strings += V.header_strings

    output = f"""// Copyright (c) Meta Platforms, Inc. and affiliates.
// THIS FILE WAS AUTOMATICALLY GENERATED BY train_model.py

// clang-format off

#ifndef {path.replace("/", "_").replace(".", "_").upper()}
#define {path.replace("/", "_").replace(".", "_").upper()}

#include "openzl/compress/selectors/ml/gbt.h"

#ifdef __cplusplus
extern "C" {{
#endif
{header_strings}
#ifdef __cplusplus
}}
#endif

#endif
"""
    if path:
        header_path = path + ".h"
        with open(header_path, "wt") as f:
            f.write(output)
    else:
        print(output)
    write_core_model(path, c_strings)


def write_header(path: str, models: t.Dict[str, str]):
    model_strings = "\n\n".join(
        f'const inline std::string_view  {K} = R"~DELIM~({V})~DELIM~";'
        for K, V in models.items()
    )
    output = f"""// Copyright (c) Meta Platforms, Inc. and affiliates.
// THIS FILE WAS AUTOMATICALLY GENERATED

#include <string>

#pragma once

namespace {{

{model_strings}


}} //namespace
"""
    if path:
        header_path = path + ".h"
        with open(header_path, "wt") as f:
            f.write(output)
    else:
        print(output)


@click.command()
@click.argument("training-samples", type=click.Path(exists=True, dir_okay=False))
@click.option(
    "-o",
    "--out",
    help="output path for the header file - please do not include file extension in the path, if empty will print to stdout",
    default="",
    type=click.Path(),
)
@click.option(
    "-m",
    "--model-name",
    help="Name of the model to generate",
    default="MODEL",
    type=click.STRING,
)
@click.option(
    "-c",
    "--core",
    is_flag=True,
    help="generate a core Zstrong predictor instead of serialized json model.",
    default=False,
)
def main(training_samples: str, out: str, model_name: str, core: bool):
    if out.endswith(".h") or out.endswith(".c"):
        print("Please do not include file extension in the path")
        return

    if not os.getcwd().endswith("data_compression/experimental/zstrong"):
        print("Please run this file from zstrong root director")
        return

    if not core:
        write_header(out, {model_name: get_trained_serialized_model(training_samples)})
    else:
        write_core_files(
            out, {model_name: get_trained_core_model(training_samples, model_name)}
        )


if __name__ == "__main__":
    main()
