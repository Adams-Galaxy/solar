#!/usr/bin/env python3
"""Production shipment verification and reproducibility checks."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from solar_codegen.shipment import build_wheel, verify_contract


class ShipmentTests(unittest.TestCase):
    def setUp(self) -> None:
        self.ir = json.loads(Path(OPTIONS.ir).read_text())
        self.manifest = self.ir["manifest"]

    def test_effective_contract_requires_exact_endpoint_set(self) -> None:
        actual = json.loads(json.dumps(self.manifest))
        actual["actions"].pop()
        self.assertTrue(any("omits action" in item for item in verify_contract(self.ir, actual)))

        actual = json.loads(json.dumps(self.manifest))
        actual["data"].append({"id": 0x7FFFFFFF, "name": "unexpected", "schema": 1})
        self.assertTrue(any("unexpected data" in item for item in verify_contract(self.ir, actual)))

    def test_effective_contract_checks_schema_and_capability_policy(self) -> None:
        actual = json.loads(json.dumps(self.manifest))
        schema = next(item for item in actual["schemas"] if item.get("fields"))
        schema["fields"][0]["id"] += 1
        self.assertTrue(any("changed fields" in item for item in verify_contract(self.ir, actual)))

        actual = json.loads(json.dumps(self.manifest))
        actual["capabilities"][0]["maximum_rate_hz"] += 1
        self.assertTrue(any("changed policy" in item for item in verify_contract(self.ir, actual)))

    def test_wheel_is_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as first_raw, tempfile.TemporaryDirectory() as second_raw:
            first = Path(first_raw)
            second = Path(second_raw)
            for root in (first, second):
                package = root / "python" / self.ir["application"]["python_package"]
                package.mkdir(parents=True)
                (package / "__init__.py").write_text("VALUE = 1\n")
            one = build_wheel(self.ir, first).read_bytes()
            two = build_wheel(self.ir, second).read_bytes()
            self.assertEqual(hashlib.sha256(one).digest(), hashlib.sha256(two).digest())

    def test_wheel_contains_typing_marker(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            package_name = self.ir["application"]["python_package"]
            package = root / "python" / package_name
            package.mkdir(parents=True)
            (package / "__init__.py").write_text("")
            (package / "py.typed").write_text("")
            wheel = build_wheel(self.ir, root)
            with zipfile.ZipFile(wheel) as archive:
                self.assertIn(f"{package_name}/py.typed", archive.namelist())


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--ir", required=True)
    OPTIONS, rest = parser.parse_known_args()
    unittest.main(argv=[__file__, *rest])
