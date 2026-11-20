#!/usr/bin/env python3
"""Write the Parquet fixtures of tests/test_parquet_view.c (needs pyarrow).

Run from the repository root: python3 tests/fixtures/parquet/make_fixtures.py
The files are committed; run this again only to change them."""
import datetime
import decimal
import os

import pyarrow as pa
import pyarrow.parquet as pq

HERE = os.path.dirname(os.path.abspath(__file__))
PREVIEW = os.path.join(HERE, "..", "preview")

UTC = datetime.timezone.utc
CITIES = ["Lyon", "Paris", "Nantes", "Lille"]


def trips(n):
    return pa.table({
        "id": pa.array(range(1, n + 1), pa.int64()),
        "city": pa.array([CITIES[i % 4] for i in range(n)], pa.string()),
        "fare": pa.array([None if i % 7 == 3 else 7.5 + i * 0.25 for i in range(n)], pa.float64()),
        "paid": pa.array([i % 3 != 0 for i in range(n)], pa.bool_()),
        "pickup": pa.array([datetime.datetime(2026, 3, 1, 8, 30, tzinfo=UTC) + datetime.timedelta(minutes=17 * i)
                            for i in range(n)], pa.timestamp("us", tz="UTC")),
        "day": pa.array([datetime.date(2026, 3, 1) + datetime.timedelta(days=i // 10) for i in range(n)], pa.date32()),
        "tip": pa.array([decimal.Decimal(i % 500) / 100 for i in range(n)], pa.decimal128(10, 2)),
        "tags": pa.array([[] if i % 5 == 0 else ["night"] * (i % 3) + ["card"] for i in range(n)],
                         pa.list_(pa.string())),
        "seats": pa.array([1 + i % 4 for i in range(n)], pa.uint8()),
    })


def main():
    t = trips(250).replace_schema_metadata({"source": "LookinFiles fixtures"})
    # Two row groups, Snappy, dictionary pages, v1 data pages
    pq.write_table(t, os.path.join(PREVIEW, "trips.parquet"), row_group_size=150, compression="snappy")
    small = trips(20)
    for codec in ("none", "gzip", "zstd", "lz4"):
        pq.write_table(small, os.path.join(HERE, f"codec-{codec}.parquet"), compression=codec)
    # v2 data pages, no dictionary, delta and byte stream split encodings
    pq.write_table(small, os.path.join(HERE, "v2-delta.parquet"), data_page_version="2.0",
                   use_dictionary=False, compression="snappy",
                   column_encoding={"id": "DELTA_BINARY_PACKED", "city": "DELTA_BYTE_ARRAY",
                                    "fare": "BYTE_STREAM_SPLIT", "seats": "DELTA_BINARY_PACKED",
                                    "pickup": "DELTA_BINARY_PACKED", "day": "DELTA_BINARY_PACKED",
                                    "tags": "DELTA_LENGTH_BYTE_ARRAY"})


if __name__ == "__main__":
    main()
