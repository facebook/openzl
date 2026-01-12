# Copyright (c) Meta Platforms, Inc. and affiliates.

# pyre-unsafe

import logging
from enum import Enum
from typing import Union

from ci_experiences.signalhub.utils.signalhub import SignalHubServiceUtils
from facebook.signal_hub.types import CISignalThriftSeverity
from libfb.py.asyncio.await_utils import await_sync
from phabricator.new_phabricator_graphql_helpers import (
    get_phabricator_diff_with_custom_return_fields,
    get_phabricator_version_with_custom_return_fields,
    PhabricatorDiffs,
)
from phabricator.phabricator_auth_strategy_factory import PhabricatorAuthStrategyFactory

log = logging.getLogger("BenchmarkRunner")

DATA_COMPRESSION_CHANNEL_ID = 1354660472066529


class PHAB_ACCESS_TYPE(Enum):
    READ = "read"
    WRITE_USER = "write"


class PhabricatorUtils:
    class SEVERITY(Enum):
        PASSED = CISignalThriftSeverity.PASSED
        WARNING = CISignalThriftSeverity.WARNING
        FAILED = CISignalThriftSeverity.FAILED
        INFO = CISignalThriftSeverity.ADVICE

    def __init__(
        self,
        signal_channel_id=DATA_COMPRESSION_CHANNEL_ID,
        access_type=PHAB_ACCESS_TYPE.READ,
    ) -> None:
        self.signal_channel_id = signal_channel_id
        if access_type == PHAB_ACCESS_TYPE.READ:
            auth_strategy = PhabricatorAuthStrategyFactory.diff_reader_bot()
        elif access_type == PHAB_ACCESS_TYPE.WRITE_USER:
            auth_strategy = PhabricatorAuthStrategyFactory.get_for_current_unix_user()
        else:
            raise RuntimeError("Invalid access type")
        self.client = PhabricatorDiffs(
            auth_strategy=auth_strategy,
            source="Data compression benchmark",
        )

    def phabricator_query_diff(self, phab_diff: str, fields: str) -> dict:
        if isinstance(phab_diff, str) and phab_diff.startswith("D"):
            phab_diff = phab_diff[1:]
        return get_phabricator_diff_with_custom_return_fields(
            self.client, phab_diff, fields
        )

    def phabricator_update_diff(self, phab_diff: str, fields: dict) -> None:
        diff_id = self.phabricator_query_diff(phab_diff, "id")["id"]
        self.client.update_phabricator_diff(diff_id, fields)

    def phabricator_get_test_plan(self, phab_diff: str) -> str:
        return self.phabricator_query_diff(phab_diff, "test_plan_plaintext")[
            "test_plan_plaintext"
        ]

    def phabricator_update_test_plan(self, phab_diff: str, test_plan: str) -> None:
        self.phabricator_update_diff(phab_diff, {"test_plan": test_plan})

    def phabricator_diff_to_latest_version_fbid(self, phab_diff: str) -> int:
        query_res = self.phabricator_query_diff(
            phab_diff, "latest_phabricator_version {id}"
        )
        version_fbid = int(query_res["latest_phabricator_version"]["id"])
        return version_fbid

    def phabricator_version_phabricator_version_fbid(
        self, phab_version: Union[str, int]
    ) -> int:
        query_res = get_phabricator_version_with_custom_return_fields(
            self.client, phab_version, "id"
        )
        version_fbid = int(query_res["id"])
        return version_fbid

    def phabricator_verion_to_diff(self, phab_version: Union[str, int]) -> str:
        return get_phabricator_version_with_custom_return_fields(
            PhabricatorUtils().client,
            phab_version,
            "associated_diff { number_with_prefix }",
        )["associated_diff"]["number_with_prefix"]

    def add_signal(
        self,
        version_fbid: int,
        title: str,
        message: str,
        severity: SEVERITY = SEVERITY.PASSED,
        deduplication_key=None,
    ):
        signal = SignalHubServiceUtils.createDiffMessageSignal(
            severity.value,
            message,
            self.signal_channel_id,
            version_fbid,
            title,
            deduplication_key=deduplication_key,
        )
        await_sync(SignalHubServiceUtils.postSignal(signal))
