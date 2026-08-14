# Copyright (c) Meta Platforms, Inc. and affiliates.


import logging
from typing import Dict, List, Optional

import numpy as np
import pandas as pd
from facebook.oncall.clients import OncallService
from libfb.py import employee
from libfb.py.asyncio.await_utils import await_sync
from pypika import Field, MySQLQuery, Order
from rfe.py.lib.sql_pandas import query_sql_as_dataframe
from rfe.scubadata.scubadata_py3 import Sample, ScubaData, ScubaWriteMode
from servicerouter.py3 import get_sr_client


log = logging.getLogger("BenchmarkRunner")


class ScubaUtils:
    TIME_COLUMN = "time"

    @classmethod
    def _df_to_scuba_samples(cls, df, lists_as_norm_vectors=False) -> List[Sample]:
        """
        Converts a DataFrame into the Scuba sample format.
        Based on Bamboo's implementation.
        """
        samples = []
        cols = list(zip(df.columns, df.dtypes))
        for row in df.itertuples():
            sample = Sample()
            for i, (column_name, column_type) in enumerate(cols, start=1):
                if column_name == cls.TIME_COLUMN:
                    sample.addTimestamp(ScubaData.TIME_COLUMN, row[i])
                elif column_type == np.int64:
                    sample.addIntValue(column_name, row[i])
                elif column_type == bool:
                    sample.addIntValue(column_name, int(row[i]))
                elif column_type == np.float64:
                    sample.addDoubleValue(column_name, row[i])
                elif column_type == object:
                    if lists_as_norm_vectors and type(row[i]) == list:
                        sample.addNormVectorValue(column_name, row[i])
                    else:
                        sample.addNormalValue(column_name, str(row[i]))
                else:
                    raise ValueError("DataFrame contains unsupported type")
            samples.append(sample)
        return samples

    @staticmethod
    async def _get_user_id():
        user_id = employee.get_current_unix_user_fbid()
        if not user_id:
            async with get_sr_client(OncallService, "oncall_service") as client:
                result = await client.getCurrentOncallForRotationByShortName(
                    "data_compression"
                )
                user_id = result.uid
        return user_id

    @staticmethod
    def _upload_samples_to_scuba(samples: List[Sample], table: str):
        with ScubaData(table, ScubaWriteMode.SEMI_RELIABLE_BLOCKING) as scubadata:
            scubadata.addSamples(samples)

    @classmethod
    def query_scuba(
        cls,
        table: str,
        subset: Optional[str] = None,
        columns: Optional[List[str]] = None,
        filters: Optional[Dict] = None,
        group_by: Optional[List[str]] = None,
        order_by: Optional[str] = None,
        limit: Optional[int] = None,
    ) -> pd.DataFrame:
        if subset:
            table = f"{table}/{subset}"
        q = MySQLQuery.from_(table)
        if columns:
            q = q.select(*columns)
        else:
            q = q.select("*")
        if filters:
            for k, v in filters.items():
                q = q.where(Field(k) == v)
        if group_by:
            for g in group_by:
                q = q.groupby(g)
        if order_by:
            q = q.orderby(Field(order_by), order=Order.desc)
        if limit is not None:
            q = q.limit(limit)
        sql = q.get_sql()
        assert sql != ""
        log.debug(f"Querying scuba: {sql}")
        user_id = await_sync(cls._get_user_id())
        return await_sync(
            query_sql_as_dataframe(
                table=table,
                sql=sql,
                source="benchmark runner utils",
                user_id=user_id,
                ignore_cast_errors=True,
            )
        )

    @classmethod
    def upload_data_to_scuba(
        cls, df: pd.DataFrame, table: str, subset: Optional[str] = None
    ):
        samples = cls._df_to_scuba_samples(df, True)
        if subset:
            for sample in samples:
                sample.setSubset(subset)
        cls._upload_samples_to_scuba(samples, table)
