# Copyright (c) Meta Platforms, Inc. and affiliates.

import json
import os
import tempfile

import typing as t

import pandas as pd
import xgboost as xgb
from zstrong_json.ml import *


class CoreModel(t.NamedTuple):
    c_strings: str
    header_strings: str


def get_serialized_predictor(
    booster: xgb.XGBClassifier,
) -> t.List[t.List[t.Dict[str, t.Any]]]:
    """
    Returns a json representation of an XGBoost booster as a GBT predictor
    """
    with tempfile.TemporaryDirectory() as tmpdirname:
        booster_path = os.path.join(tmpdirname, "booster.json")
        booster.save_model(booster_path)
        with open(booster_path, "rb") as f:
            jbst = json.loads(f.read())
    model = jbst["learner"]["gradient_booster"]["model"]
    forests = [[] for _ in range(max(model["tree_info"]) + 1)]
    for tree, forestIdx in zip(model["trees"], model["tree_info"]):
        featureIdx = [
            i if l != -1 else -1
            for i, l in zip(tree["split_indices"], tree["left_children"])
        ]
        forests[forestIdx].append(
            {
                "featureIdx": featureIdx,
                "value": tree["split_conditions"],
                "leftChildIdx": tree["left_children"],
                "rightChildIdx": tree["right_children"],
                "defaultLeft": [int(i) for i in tree["default_left"]],
            }
        )
    return forests


def _get_node_strings(tree: t.Dict[str, t.Any]) -> t.Tuple[int, str]:
    """
    Returns a tuple representing the number of nodes in a tree, and a string representing an array of nodes
    """
    node_strings = []
    for feature_ind, feature in enumerate(tree["featureIdx"]):
        left_ind = max(tree["leftChildIdx"][feature_ind], 0)
        right_ind = max(tree["rightChildIdx"][feature_ind], 0)
        node_val = str(tree["value"][feature_ind])
        missing_ind = left_ind if tree["defaultLeft"][feature_ind] else right_ind

        node_str = f"""
                        {{
                            .featureIdx      = {feature},
                            .value           = {node_val}f,
                            .leftChildIdx    = {left_ind},
                            .rightChildIdx   = {right_ind},
                            .missingChildIdx = {missing_ind}
                        }}"""

        node_strings.append(node_str)
    # Join the list of nodes into a string representing a node array
    return (len(node_strings), ",".join(node_strings))


def _get_tree_strings(forest: t.List[t.Dict[str, t.Any]]) -> t.Tuple[int, str]:
    """
    Returns a tuple representing the number of trees in a forest, and a string representing an array of trees
    """
    tree_strings = []
    for tree in forest:
        num_nodes, node_strings = _get_node_strings(tree)
        # Create a string representation of a tree containing nodes
        tree_data_str = f"""
                {{
                    .numNodes = {num_nodes},
                    .nodes = (GBTPredictor_Node[])
                    {{{node_strings}
                    }} 
                }}"""

        tree_strings.append(tree_data_str)

    return (len(tree_strings), ",".join(tree_strings))


def _get_forest_strings(
    serialized_pred: t.List[t.List[t.Dict[str, t.Any]]],
) -> t.Tuple[int, str]:
    """
    Returns a tuple representing the total number of forests, and a string representing an array of forests
    """
    forest_strings = []

    for forest in serialized_pred:
        num_trees, tree_strings = _get_tree_strings(forest)
        forest_str = f"""
        {{
            .numTrees = {num_trees},
            .trees = (GBTPredictor_Tree[])
            {{{tree_strings}
            }}
        }}
        """

        # Append stringified tree array to corresponding forest
        forest_strings.append(forest_str)

    return (len(forest_strings), ",".join(forest_strings))


def get_core_predictor(
    booster: xgb.XGBClassifier,
    prefix: str,
) -> str:
    """
    Returns a string representing the predictor.
    """

    num_forests, forest_strings = _get_forest_strings(get_serialized_predictor(booster))

    structured_predictor_str = f"""
static const GBTPredictor {prefix}_PREDICTOR = {{
    .numForests = {num_forests},
    .forests = (GBTPredictor_Forest[])
    {{{forest_strings}
    }}
}};
"""

    return structured_predictor_str


def _get_labels(arr_name: str, nb_name: str, labels: t.List[str]) -> str:
    """
    Returns a string representing an array of Labels and the number of labels
    """
    c_strings = f"static const Label {arr_name}[] = "
    c_strings += "{" + ", ".join(f'"{label}"' for label in labels) + "};\n"
    c_strings += f"static const size_t {nb_name} = {len(labels)};\n"
    return c_strings


def _get_gbt_model_func(prefix: str, func_name: str) -> str:
    func_str = """
GBTModel {func_name}(FeatureGenerator featureGenerator)
{{    GBTModel gbtModel = {{
        .predictor        = &{predictor},
        .featureGenerator = featureGenerator,
        .nbLabels         = {nbLabels},
        .classLabels      = {classLabels},
        .nbFeatures       = {nbFeatures},
        .featureLabels    = {featureLabels},
    }};
    return gbtModel;
}}
    """.format(
        func_name=func_name,
        predictor=f"{prefix}_PREDICTOR",
        nbLabels=f"{prefix}_NB_LABELS",
        classLabels=f"{prefix}_CLASS_LABELS",
        nbFeatures=f"{prefix}_NB_FEATURES",
        featureLabels=f"{prefix}_FEATURE_LABELS",
    )
    return func_str


def get_core_model(
    booster: xgb.XGBClassifier, features: t.List[str], labels: t.List[str], prefix: str
) -> CoreModel:
    """
    Return a dictionary containing two keys: c_strings and header_strings

    The 'c_strings' key maps to a string that contain the implementation details of
    the predictor, feature lists and class labels.

    The 'header_strings' key maps to a string that contains extern getter function
    that can be used to create a core model.
    """

    c_strings = get_core_predictor(booster, prefix)
    header_strings = ""

    # Append the class labels
    class_labels = f"{prefix}_CLASS_LABELS"
    nb_labels = f"{prefix}_NB_LABELS"
    c_strings += _get_labels(class_labels, nb_labels, labels)

    # Append the feature labels
    feature_labels = f"{prefix}_FEATURE_LABELS"
    nb_features = f"{prefix}_NB_FEATURES"
    c_strings += _get_labels(feature_labels, nb_features, features)

    # Create the extern getter function declarations
    func_name = f" GET_{prefix}_GBT_MODEL"
    func_name = "".join(x.capitalize() for x in func_name.lower().split("_"))

    header_strings += f"\n// GENERATED {prefix} MODEL GETTER FUNCTION\n"
    header_strings += (
        f"extern GBTModel {func_name}(FeatureGenerator featureGenerator);\n"
    )

    c_strings += _get_gbt_model_func(prefix, func_name)
    return CoreModel(c_strings=c_strings, header_strings=header_strings)


def get_serialized_model(
    booster: xgb.XGBClassifier, features: t.List[str], labels: t.List[str]
) -> str:
    """
    Returns a json representation of a model, including the booster, ordered feature list and ordered labels
    """
    zstrong_gbt_model = {
        "predictor": get_serialized_predictor(booster),
        "features": [str(c) for c in features],
        "labels": [str(c) for c in labels],
    }
    return json.dumps(zstrong_gbt_model)


def process_training_samples(
    samples: t.List[MLTrainingSample],
    feature_generator: t.Optional[BaseFeatureGenerator] = None,
    choice_function: t.Optional[
        t.Callable[[t.Dict[str, float], t.Dict[str, float]], str]
    ] = None,
) -> t.Tuple[pd.DataFrame, pd.DataFrame]:
    """
    Creates a features and results dataframe for given samples.
    First returned dataframe is features and should be used as X dataset when training.
    Second returned dataframe is results and it contains metrics for each of the labels and
    the target we should choose in the `choice` column. This column should be used as the target
    for classification.
    If a `feature_generator` is given the features will be calculated for the `data` field in each
    sample, if not they will be taken from the `features` member of each sample.
    If a `choice_function` is given it will be used to select the target, otherwise we choose
    the target with smallest size. The `choice_function` should take two arguments, the first
    being a dictionary of sizes and the second one containing compression times for each label.
    The function should return the name of the target to choose.
    """
    targets = []
    for s in samples:
        labels = list(s.targets.keys())
        sizes = {t: s.targets[t]["size"] for t in labels}
        ctimes = {f"{t}": s.targets[t]["ctime"] for t in labels}
        if choice_function:
            choice = choice_function(sizes, ctimes)
        else:
            choice = min(sizes, key=lambda k: sizes[k])
        target = {**sizes}
        target["choice"] = choice
        target.update({f"{t}_ctime": ctimes[t] for t in ctimes})
        target["best_size"] = min(sizes.values())
        targets.append(target)
    df_targets = pd.DataFrame(targets)

    if feature_generator:
        df_features = pd.DataFrame(
            [feature_generator.getFeatures(r.data) for r in samples]
        )
    else:
        df_features = pd.DataFrame([r.features for r in samples])

    return df_features, df_targets
