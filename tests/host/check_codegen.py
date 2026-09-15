#!/usr/bin/env python3
"""Tests for the hermetic Solar project/IDL compiler."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class CodegenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generator = Path(OPTIONS.generator).resolve()
        cls.fixture = Path(OPTIONS.fixture).resolve()
        sys.path.insert(0, str(cls.generator.parent))
        from solar_codegen import CompileError, compile_project

        cls.CompileError = CompileError
        cls.compile_project = staticmethod(compile_project)

    def copy_fixture(self, root: Path) -> Path:
        destination = root / "fixture"
        shutil.copytree(self.fixture, destination)
        return destination

    def generate(self, fixture: Path, output: Path, lock: Path):
        return subprocess.run(
            [
                sys.executable,
                str(self.generator),
                "--project",
                str(fixture / "solar.project.yaml"),
                "--output",
                str(output),
                "--lock",
                str(lock),
                "--update-lock",
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_golden_ir_and_determinism(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            first = root / "first"
            second = root / "second"
            third = root / "third"
            result = self.generate(fixture, first, lock)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = self.generate(fixture, second, lock)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = self.generate(fixture, third, lock)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in (
                "interface.ir.json",
                "manifest.json",
                "compatibility.json",
                "solar/generated/types.hpp",
                "solar/generated/parameters.hpp",
                "solar/generated/contract.hpp",
                "solar/generated/app.hpp",
                "solar/generated/application.hpp",
                "solar/generated/application.cpp",
            ):
                self.assertEqual(
                    (second / name).read_bytes(), (third / name).read_bytes()
                )
            ir = json.loads((first / "interface.ir.json").read_text())
            self.assertEqual(ir["application"]["namespace"], "fixture_app")
            self.assertEqual(len(ir["parameters"]), 1)
            self.assertEqual(len(ir["actions"]), 1)
            self.assertEqual(len(ir["stream_declarations"]), 2)
            self.assertEqual(
                {item["name"] for item in ir["manifest"]["streams"]},
                {"drive.command", "imu.euler"},
            )
            self.assertNotIn(
                "imu.euler", [item["name"] for item in ir["manifest"]["data"]]
            )
            self.assertNotIn(
                "drive.command", [item["name"] for item in ir["manifest"]["data"]]
            )
            publication = next(
                item
                for item in ir["manifest"]["capabilities"]
                if item["endpoint"]
                == next(
                    stream["id"]
                    for stream in ir["manifest"]["streams"]
                    if stream["name"] == "imu.euler"
                )
            )
            self.assertEqual(publication["domain"], "stream")
            self.assertEqual(publication["kind"], "out_stream")
            self.assertEqual(
                ir["dependencies"], ["solar.project.yaml", "robot.solar.yaml"]
            )
            self.assertEqual(ir["parameters"][0]["source"]["file"], "robot.solar.yaml")
            self.assertGreater(ir["parameters"][0]["source"]["line"], 0)
            self.assertIn(
                "ParameterSchema",
                (first / "solar/generated/parameters.hpp").read_text(),
            )
            application = (first / "solar/generated/application.hpp").read_text()
            contract = (first / "solar/generated/contract.hpp").read_text()
            self.assertIn("GeneratedTraits<fixture_app::Application>", application)
            self.assertIn(
                "namespace fixture_app::contract::types",
                (first / "solar/generated/types.hpp").read_text(),
            )
            self.assertIn("namespace fixture_app::contract::actions::system", contract)
            self.assertIn("using Handle = solar::endpoint::Handle", contract)
            self.assertIn("using Input = solar::endpoint::Input", contract)

    def test_explain_and_feature_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            output = root / "output"
            self.assertEqual(self.generate(fixture, output, lock).returncode, 0)
            explained = subprocess.run(
                [
                    sys.executable,
                    str(self.generator),
                    "explain",
                    "application",
                    "--project",
                    str(fixture / "solar.project.yaml"),
                    "--output",
                    str(output),
                    "--lock",
                    str(lock),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(explained.returncode, 0, explained.stderr)
            self.assertIn("Solar application: robot-fixture", explained.stdout)
            self.assertTrue((output / "application.json").is_file())
            self.assertTrue((output / "application.txt").is_file())

            unavailable = subprocess.run(
                [
                    sys.executable,
                    str(self.generator),
                    "--project",
                    str(fixture / "solar.project.yaml"),
                    "--output",
                    str(root / "unavailable"),
                    "--lock",
                    str(lock),
                    "--validate-features",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(unavailable.returncode, 0)
            self.assertIn("CONFIG_SOLAR_REMOTE", unavailable.stderr)
            self.assertIn("solar.project.yaml:", unavailable.stderr)

    def test_clean_directories_are_byte_identical(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first_fixture = root / "one" / "fixture"
            second_fixture = root / "two" / "fixture"
            shutil.copytree(self.fixture, first_fixture)
            shutil.copytree(self.fixture, second_fixture)
            first_output = root / "one" / "output"
            second_output = root / "two" / "output"
            self.assertEqual(
                self.generate(
                    first_fixture, first_output, first_fixture / "solar.interface.lock"
                ).returncode,
                0,
            )
            self.assertEqual(
                self.generate(
                    second_fixture,
                    second_output,
                    second_fixture / "solar.interface.lock",
                ).returncode,
                0,
            )
            for relative in (
                "interface.ir.json",
                "manifest.json",
                "compatibility.json",
                "solar/generated/types.hpp",
                "solar/generated/parameters.hpp",
                "solar/generated/contract.hpp",
                "solar/generated/app.hpp",
                "python/robot_fixture_solar/models.py",
                "python/robot_fixture_solar/client.py",
            ):
                self.assertEqual(
                    (first_output / relative).read_bytes(),
                    (second_output / relative).read_bytes(),
                )

    def test_unknown_type_has_source_location(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "type: f32\n        unit: rad",
                    "type: mystery\n        unit: rad",
                    1,
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("robot.solar.yaml:", str(caught.exception))
            self.assertIn("unknown type 'mystery'", str(caught.exception))

    def test_unknown_key_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "    maximum: 10.0", "    maximum: 10.0\n    maximim: 11.0"
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn(
                "unknown parameter drive.kp key 'maximim'", str(caught.exception)
            )
            self.assertIn("robot.solar.yaml:", str(caught.exception))

    def test_metadata_does_not_coerce_non_strings(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "  drive.kp:\n    type: f32",
                    "  drive.kp:\n    description: 42\n    type: f32",
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn(
                "parameter description must be a string", str(caught.exception)
            )

    def test_boolean_fields_do_not_use_truthiness(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace("    open: false", "    open: disabled")
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("enum open must be true or false", str(caught.exception))

    def test_invalid_explicit_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "  system.ping:\n    request:",
                    "  system.ping:\n    id: 0\n    request:",
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("id must be an integer from 1", str(caught.exception))

    def test_duplicate_field_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text()
                .replace(
                    "      throttle: f32",
                    "      throttle:\n        id: 4\n        type: f32",
                )
                .replace(
                    "      differential: f32",
                    "      differential:\n        id: 4\n        type: f32",
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("identity collision", str(caught.exception))
            self.assertIn("DriveCommand", str(caught.exception))

    def test_invalid_declaration_version_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "  system.ping:\n    request:",
                    "  system.ping:\n    version: 0\n    request:",
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("version must be a positive integer", str(caught.exception))

    def test_bounded_and_optional_types_are_normalized(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            text = interface.read_text()
            marker = "parameters:\n"
            extra = (
                "  BoundedSample:\n"
                "    kind: struct\n"
                "    fields:\n"
                "      label: string<32>\n"
                "      payload: bytes<64>\n"
                "      samples: sequence<u16, 8>\n"
                "      axes: array<f32, 3>\n"
                "      note:\n"
                "        type: string<16>\n"
                "        optional: true\n"
            )
            interface.write_text(text.replace(marker, extra + marker))
            ir, _, _ = self.compile_project(fixture / "solar.project.yaml")
            bounded = next(
                item for item in ir["types"] if item["cpp_name"] == "BoundedSample"
            )
            fields = {item["name"]: item["resolved"] for item in bounded["fields"]}
            self.assertEqual(fields["label"]["cpp"], "solar::BoundedText<32>")
            self.assertEqual(fields["payload"]["maximum_length"], 64)
            self.assertEqual(fields["samples"]["kind"], "sequence")
            self.assertEqual(fields["axes"]["kind"], "array")
            self.assertEqual(fields["note"]["kind"], "optional")

    def test_record_shape_array_field_end_to_end(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            text = interface.read_text()
            marker = "parameters:\n"
            extra = (
                "  ScanPoint:\n"
                "    kind: struct\n"
                "    shape: record\n"
                "    fields:\n"
                "      angle: u16\n"
                "      distance: u16\n"
                "      confidence: u8\n"
                "  Scan:\n"
                "    kind: struct\n"
                "    fields:\n"
                "      generation: u64\n"
                "      points: sequence<ScanPoint, 4>\n"
                "      checksums: sequence<u16, 4>\n"
            )
            interface.write_text(text.replace(marker, extra + marker))
            ir, _, _ = self.compile_project(fixture / "solar.project.yaml")

            schemas = {item["name"]: item for item in ir["manifest"]["schemas"]}
            point = schemas["robot.fixture.ScanPoint"]
            self.assertEqual(point["shape"], "record")

            scan = schemas["robot.fixture.Scan"]
            self.assertEqual(scan["shape"], "object")
            fields = {item["name"]: item for item in scan["fields"]}
            self.assertEqual(fields["points"]["kind"], "array")
            self.assertEqual(fields["points"]["element_kind"], "schema")
            self.assertEqual(fields["points"]["schema"], point["id"])
            self.assertEqual(fields["points"]["maximum_length"], 4)
            self.assertIsNone(fields["generation"]["element_kind"])
            self.assertEqual(fields["checksums"]["kind"], "array")
            self.assertEqual(fields["checksums"]["element_kind"], "unsigned")
            self.assertIsNone(fields["checksums"]["schema"])

            # A Record schema is reference-only, the same as an Enumeration --
            # it carries no independent wire codec/size of its own at the C++
            # level (schema_codec/schema_maximum emit 0/"none" for it), and it
            # must still be explicitly contributed to the catalog even though
            # nothing references it as a top-level Data/Action/Stream value.
            self.assertEqual(point["codec"], "none")
            self.assertEqual(point["max_encoded_size"], 0)

            output_dir = Path(temporary) / "generated"
            self.generate(fixture, output_dir, fixture / "solar.interface.lock")
            remote_text = (output_dir / "solar" / "generated" / "remote.hpp").read_text()
            self.assertIn(
                "using RemoteSchemas = solar::remote::ContributeSchemas<",
                remote_text,
            )
            contribution = remote_text.split(
                "using RemoteSchemas = solar::remote::ContributeSchemas<"
            )[1].split(">;")[0]
            self.assertIn("ScanPoint", contribution)

            self.assertIn(
                "struct solar::remote::Schema<fixture_app::generated::ScanPoint>",
                remote_text,
            )
            record_block = remote_text.split(
                "struct solar::remote::Schema<fixture_app::generated::ScanPoint>"
            )[1].split("\n};\n")[0]
            self.assertIn("SchemaShape::Record", record_block)
            self.assertNotIn("max_encoded_size", record_block)
            self.assertNotIn("codec = Codec::Cbor", record_block)

    def test_record_shape_rejects_unsupported_fields(self):
        cases = {
            "optional field": ("value:\n        type: u16\n        optional: true\n", "must not be optional"),
            "bytes field": ("value: bytes<8>\n", "must be a fixed-width scalar or enum"),
            "array field": ("value: sequence<u16, 4>\n", "must be a fixed-width scalar or enum"),
            "nested struct field": ("value: Euler\n", "must not reference another struct"),
        }
        for label, (field_yaml, message) in cases.items():
            with self.subTest(label):
                with tempfile.TemporaryDirectory() as temporary:
                    fixture = self.copy_fixture(Path(temporary))
                    interface = fixture / "robot.solar.yaml"
                    text = interface.read_text()
                    marker = "parameters:\n"
                    extra = (
                        "  BadRecord:\n"
                        "    kind: struct\n"
                        "    shape: record\n"
                        "    fields:\n"
                        f"      {field_yaml}"
                    )
                    interface.write_text(text.replace(marker, extra + marker))
                    with self.assertRaises(self.CompileError) as caught:
                        self.compile_project(fixture / "solar.project.yaml")
                    self.assertIn(message, str(caught.exception))

    def test_output_stream_delivery_policy_end_to_end(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            text = interface.read_text()
            marker = "streams:\n"
            extra = (
                "  imu.queued:\n"
                "    type: Euler\n"
                "    direction: out\n"
                "    maximum-rate: 20\n"
                "    delivery: queue<3, drop-newest>\n"
                "  imu.reliable:\n"
                "    type: Euler\n"
                "    direction: out\n"
                "    maximum-rate: 10\n"
                "    delivery: reliable<4>\n"
            )
            interface.write_text(text.replace(marker, marker + extra))
            ir, _, _ = self.compile_project(fixture / "solar.project.yaml")

            capabilities_by_endpoint = {
                item["endpoint"]: item for item in ir["manifest"]["capabilities"]
            }
            streams_by_name = {item["name"]: item for item in ir["manifest"]["streams"]}
            queued = capabilities_by_endpoint[streams_by_name["imu.queued"]["id"]]
            self.assertEqual(queued["delivery"], "queue_drop_newest")
            self.assertEqual(queued["reliable_window"], 0)
            self.assertEqual(queued["maximum_rate_hz"], 20)
            reliable = capabilities_by_endpoint[streams_by_name["imu.reliable"]["id"]]
            self.assertEqual(reliable["delivery"], "reliable")
            self.assertEqual(reliable["reliable_window"], 4)
            self.assertEqual(reliable["maximum_rate_hz"], 10)
            # The pre-existing `imu.euler` stream (no `delivery:`) still
            # defaults to plain `latest` semantics.
            latest = capabilities_by_endpoint[streams_by_name["imu.euler"]["id"]]
            self.assertEqual(latest["delivery"], "latest")
            self.assertEqual(latest["reliable_window"], 0)

            output_dir = Path(temporary) / "generated"
            self.generate(fixture, output_dir, fixture / "solar.interface.lock")
            remote_text = (output_dir / "solar" / "generated" / "remote.hpp").read_text()

            queued_block = remote_text.split("struct ImuQueuedStreamRemote")[1].split(
                "\n};\n"
            )[0]
            self.assertIn("template <typename Endpoints>", remote_text)
            self.assertIn(
                "solar::remote::OutStream<solar::remote::Push, "
                "solar::remote::Queue<3, solar::remote::DropNewest>, "
                "solar::remote::MaxRate<20>>",
                queued_block,
            )
            self.assertIn(
                "static std::optional<Value> publish() noexcept { return "
                "Endpoints::template publish<ImuQueuedStream>(); }",
                queued_block,
            )
            self.assertIn("using ContractType = ImuQueuedStream;", queued_block)

            reliable_block = remote_text.split("struct ImuReliableStreamRemote")[1].split(
                "\n};\n"
            )[0]
            self.assertIn(
                "solar::remote::OutStream<solar::remote::Push, "
                "solar::remote::ReliableWindow<4>, solar::remote::MaxRate<10>>",
                reliable_block,
            )

            latest_block = remote_text.split("struct ImuEulerStreamRemote")[1].split(
                "\n};\n"
            )[0]
            self.assertIn(
                "using Capabilities = solar::remote::Capabilities<solar::remote::OutStream<"
                "solar::remote::Push, solar::remote::MaxRate<100>>>;",
                latest_block,
            )

    def test_output_stream_delivery_rejected_on_input_direction(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            text = interface.read_text()
            interface.write_text(
                text.replace(
                    "    direction: in\n    maximum-rate: 50\n",
                    "    direction: in\n    maximum-rate: 50\n    delivery: reliable<4>\n",
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("only output streams may declare a delivery policy", str(caught.exception))

    def test_unbounded_string_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "      sequence: u32", "      sequence: string"
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("unknown type 'string'", str(caught.exception))

    def test_duplicate_name_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            project = fixture / "solar.project.yaml"
            project.write_text(
                project.read_text().replace(
                    "  - robot.solar.yaml", "  - robot.solar.yaml\n  - robot.solar.yaml"
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(project)
            self.assertIn("duplicate types declaration", str(caught.exception))

    def test_invalid_default_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace("default: 1.0", "default: 20.0")
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("default violates", str(caught.exception))

    def test_malformed_bounds_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace("minimum: 0.0", "minimum: 11.0")
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("minimum must not exceed maximum", str(caught.exception))

    def test_incompatible_bound_type_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace("maximum: 10.0", "maximum: invalid")
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("maximum has an incompatible type", str(caught.exception))

    def test_lock_ids_survive_reordering(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            first = root / "first"
            self.assertEqual(self.generate(fixture, first, lock).returncode, 0)
            before = json.loads(lock.read_text())
            interface = fixture / "robot.solar.yaml"
            text = interface.read_text()
            text = text.replace(
                "      manual: 0\n      autonomous: 1",
                "      autonomous: 1\n      manual: 0",
            )
            interface.write_text(text)
            second = root / "second"
            self.assertEqual(self.generate(fixture, second, lock).returncode, 0)
            after = json.loads(lock.read_text())
            self.assertEqual(
                {key: value["id"] for key, value in before["entries"].items()},
                {key: value["id"] for key, value in after["entries"].items()},
            )

    def test_rename_preserves_ids_and_is_compatible(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            self.assertEqual(self.generate(fixture, root / "first", lock).returncode, 0)
            before = json.loads(lock.read_text())
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "  drive.kp:\n    type: f32",
                    "  drive.gain:\n    renamed-from: drive.kp\n    type: f32",
                )
            )
            output = root / "second"
            result = self.generate(fixture, output, lock)
            self.assertEqual(result.returncode, 0, result.stderr)
            after = json.loads(lock.read_text())
            self.assertEqual(
                before["entries"]["parameter:drive.kp"]["id"],
                after["entries"]["parameter:drive.gain"]["id"],
            )
            changes = json.loads((output / "compatibility.json").read_text())["changes"]
            self.assertTrue(
                any(
                    item["kind"] == "compatible"
                    and item["declaration"] == "parameter:drive.gain"
                    and item["renamed_from"] == "parameter:drive.kp"
                    for item in changes
                )
            )

    def test_unknown_rename_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "  drive.kp:\n    type: f32",
                    "  drive.gain:\n    renamed-from: missing.gain\n    type: f32",
                )
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("rename source", str(caught.exception))

    def test_rename_chain_uses_latest_locked_name(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            self.assertEqual(self.generate(fixture, root / "first", lock).returncode, 0)
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "  drive.kp:\n    type: f32",
                    "  drive.gain:\n    renamed-from: drive.kp\n    type: f32",
                )
            )
            self.assertEqual(
                self.generate(fixture, root / "second", lock).returncode, 0
            )
            interface.write_text(
                interface.read_text().replace(
                    "  drive.gain:\n    renamed-from: drive.kp\n    type: f32",
                    "  drive.proportional:\n    renamed-from: drive.gain\n    type: f32",
                )
            )
            result = self.generate(fixture, root / "third", lock)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_rename_chain_cannot_skip_retired_name(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            self.assertEqual(self.generate(fixture, root / "first", lock).returncode, 0)
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "  drive.kp:\n    type: f32",
                    "  drive.gain:\n    renamed-from: drive.kp\n    type: f32",
                )
            )
            self.assertEqual(
                self.generate(fixture, root / "second", lock).returncode, 0
            )
            interface.write_text(
                interface.read_text().replace(
                    "  drive.gain:\n    renamed-from: drive.kp\n    type: f32",
                    "  drive.proportional:\n    renamed-from: drive.kp\n    type: f32",
                )
            )
            result = self.generate(fixture, root / "invalid", lock)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("rename source", result.stderr)

    def test_unsupported_lock_format_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            lock.write_text('{"format": 999, "entries": {}, "retired": {}}\n')
            result = self.generate(fixture, root / "output", lock)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsupported interface lock format", result.stderr)

    def test_retired_id_cannot_be_reused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            self.assertEqual(self.generate(fixture, root / "first", lock).returncode, 0)
            old_id = json.loads(lock.read_text())["entries"]["action:system.ping"]["id"]
            interface = fixture / "robot.solar.yaml"
            text = interface.read_text()
            action_begin = text.index("actions:\n")
            streams_begin = text.index("stream-groups:\n")
            interface.write_text(
                text[:action_begin] + "actions:\n" + text[streams_begin:]
            )
            self.assertEqual(
                self.generate(fixture, root / "retired", lock).returncode, 0
            )
            text = interface.read_text()
            streams_begin = text.index("stream-groups:\n")
            action = (
                "actions:\n"
                "  replacement.ping:\n"
                f"    id: {old_id}\n"
                "    request: Empty\n"
                "    response: PingResponse\n"
            )
            interface.write_text(
                text[: text.index("actions:\n")] + action + text[streams_begin:]
            )
            result = self.generate(fixture, root / "reused", lock)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("identity collision", result.stderr)

    def test_duplicate_explicit_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            text = (
                interface.read_text()
                .replace(
                    "  imu.euler:\n    type:", "  imu.euler:\n    id: 4660\n    type:"
                )
                .replace(
                    "  drive.command:\n    type:",
                    "  drive.command:\n    id: 4660\n    type:",
                )
            )
            interface.write_text(text)
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("identity collision", str(caught.exception))

    def test_configured_declaration_bound_is_enforced(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            project = fixture / "solar.project.yaml"
            project.write_text(
                project.read_text().replace("maximum-types: 32", "maximum-types: 1")
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(project)
            self.assertIn("exceeds configured maximum 1", str(caught.exception))

    def test_breaking_change_is_classified(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            self.assertEqual(self.generate(fixture, root / "first", lock).returncode, 0)
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace("maximum: 10.0", "maximum: 8.0")
            )
            output = root / "second"
            self.assertEqual(self.generate(fixture, output, lock).returncode, 0)
            changes = json.loads((output / "compatibility.json").read_text())["changes"]
            self.assertTrue(
                any(
                    item["kind"] == "breaking"
                    and item["declaration"] == "parameter:drive.kp"
                    for item in changes
                )
            )

    def test_documentation_change_does_not_break_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            self.assertEqual(self.generate(fixture, root / "first", lock).returncode, 0)
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text().replace(
                    "  drive.kp:\n    type: f32",
                    "  drive.kp:\n    description: Tunable gain.\n    type: f32",
                )
            )
            output = root / "second"
            self.assertEqual(self.generate(fixture, output, lock).returncode, 0)
            changes = json.loads((output / "compatibility.json").read_text())["changes"]
            self.assertFalse(
                any(item["declaration"] == "parameter:drive.kp" for item in changes)
            )

    def test_python_path_collision_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            interface.write_text(
                interface.read_text()
                + "  imu-euler:\n"
                + "    type: Euler\n"
                + "    direction: out\n"
                + "  imu_euler:\n"
                + "    type: Euler\n"
                + "    direction: out\n"
            )
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("same Python path", str(caught.exception))

    def test_recursive_layout_is_rejected_at_semantic_stage(self):
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.copy_fixture(Path(temporary))
            interface = fixture / "robot.solar.yaml"
            text = interface.read_text().replace(
                "      yaw:\n        type: f32\n        unit: rad",
                "      yaw:\n        type: f32\n        unit: rad\n"
                "      parent:\n        type: optional<Euler>",
            )
            interface.write_text(text)
            with self.assertRaises(self.CompileError) as caught:
                self.compile_project(fixture / "solar.project.yaml")
            self.assertIn("recursive firmware type layout", str(caught.exception))

    def test_reordering_fields_preserves_lock_managed_ids(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = self.copy_fixture(root)
            lock = fixture / "solar.interface.lock"
            first = root / "first"
            self.assertEqual(self.generate(fixture, first, lock).returncode, 0)
            before = {
                field["name"]: field["id"]
                for schema in json.loads((first / "manifest.json").read_text())[
                    "schemas"
                ]
                if schema["name"] == "robot.fixture.Euler"
                for field in schema["fields"]
            }
            interface = fixture / "robot.solar.yaml"
            text = interface.read_text()
            old = (
                "      roll:\n        type: f32\n        unit: rad\n"
                "      pitch:\n        type: f32\n        unit: rad\n"
                "      yaw:\n        type: f32\n        unit: rad"
            )
            new = (
                "      yaw:\n        type: f32\n        unit: rad\n"
                "      roll:\n        type: f32\n        unit: rad\n"
                "      pitch:\n        type: f32\n        unit: rad"
            )
            interface.write_text(text.replace(old, new))
            second = root / "second"
            self.assertEqual(self.generate(fixture, second, lock).returncode, 0)
            after = {
                field["name"]: field["id"]
                for schema in json.loads((second / "manifest.json").read_text())[
                    "schemas"
                ]
                if schema["name"] == "robot.fixture.Euler"
                for field in schema["fields"]
            }
            self.assertEqual(before, after)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", required=True)
    parser.add_argument("--fixture", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
