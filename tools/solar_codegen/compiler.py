#!/usr/bin/env python3
"""Hermetic compiler for Solar project and interface declarations.

The frontend intentionally accepts a strict, small YAML subset. It has no
implicit YAML scalar coercions and retains source locations for diagnostics.
All backends consume the normalized JSON-compatible IR produced here.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import keyword
from pathlib import Path
import re
import sys
from typing import Any, Iterable

from .diagnostics import CompileError, SourceLocation
from .identity import IdentityAllocator

from .versions import (
    COMPATIBILITY_FORMAT,
    EFFECTIVE_MANIFEST_FORMAT,
    GENERATOR_VERSION,
    INTERFACE_FORMAT,
    IR_FORMAT,
    LOCK_FORMAT,
    PROJECT_FORMAT,
    REMOTE_PROTOCOL,
)

IR_VERSION = IR_FORMAT
LOCK_VERSION = LOCK_FORMAT


@dataclass(slots=True)
class ParsedDocument:
    value: dict[str, Any]
    locations: dict[tuple[str | int, ...], SourceLocation]


@dataclass(frozen=True, slots=True)
class _Line:
    number: int
    indent: int
    text: str


def _scalar(text: str, location: SourceLocation) -> Any:
    if not text:
        raise CompileError("empty scalar", location)
    if text.startswith('"'):
        try:
            value = json.loads(text)
        except json.JSONDecodeError as error:
            raise CompileError(
                f"invalid quoted string: {error.msg}", location
            ) from error
        if not isinstance(value, str):
            raise CompileError("quoted scalar must be a string", location)
        return value
    if text.startswith("'"):
        if len(text) < 2 or not text.endswith("'"):
            raise CompileError("unterminated single-quoted string", location)
        return text[1:-1].replace("''", "'")
    if text in ("true", "false"):
        return text == "true"
    if text == "null":
        return None
    if text.lower() in ("yes", "no", "on", "off", "~"):
        raise CompileError(
            "implicit YAML boolean/null spellings are forbidden; use true, false, or null",
            location,
        )
    if re.fullmatch(r"[-+]?(?:0|[1-9][0-9]*)", text):
        return int(text, 10)
    if re.fullmatch(r"0x[0-9A-Fa-f]+", text):
        return int(text, 16)
    if re.fullmatch(
        r"[-+]?(?:(?:[0-9]+\.[0-9]*)|(?:[0-9]*\.[0-9]+))(?:[eE][-+]?[0-9]+)?",
        text,
    ):
        return float(text)
    if text.startswith("[") or text.startswith("{"):
        raise CompileError("flow-style collections are not supported", location)
    return text


def parse_strict_yaml(path: Path) -> ParsedDocument:
    lines: list[_Line] = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        if "\t" in raw[: len(raw) - len(raw.lstrip())]:
            raise CompileError(
                "tabs are forbidden for indentation", SourceLocation(path, number, 1)
            )
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        if indent % 2:
            raise CompileError(
                "indentation must use multiples of two spaces",
                SourceLocation(path, number, 1),
            )
        lines.append(_Line(number, indent, raw[indent:]))

    locations: dict[tuple[str | int, ...], SourceLocation] = {}

    def parse_block(index: int, indent: int, node_path: tuple[str | int, ...]):
        if index >= len(lines) or lines[index].indent < indent:
            raise CompileError("expected an indented value", SourceLocation(path, 1, 1))
        is_list = lines[index].text.startswith("- ")
        result: Any = [] if is_list else {}
        while index < len(lines):
            line = lines[index]
            if line.indent < indent:
                break
            if line.indent > indent:
                raise CompileError(
                    "unexpected indentation",
                    SourceLocation(path, line.number, line.indent + 1),
                )
            if is_list:
                if not line.text.startswith("- "):
                    raise CompileError(
                        "cannot mix mapping and sequence entries",
                        SourceLocation(path, line.number, line.indent + 1),
                    )
                item_text = line.text[2:].strip()
                item_path = node_path + (len(result),)
                location = SourceLocation(path, line.number, line.indent + 3)
                locations[item_path] = location
                if not item_text:
                    if index + 1 >= len(lines) or lines[index + 1].indent <= indent:
                        raise CompileError("sequence item requires a value", location)
                    item, index = parse_block(
                        index + 1, lines[index + 1].indent, item_path
                    )
                    result.append(item)
                    continue
                result.append(_scalar(item_text, location))
                index += 1
                continue

            if line.text.startswith("- ") or ":" not in line.text:
                raise CompileError(
                    "mapping entry must be 'name: value'",
                    SourceLocation(path, line.number, line.indent + 1),
                )
            key_text, value_text = line.text.split(":", 1)
            key = key_text.strip()
            if not key or not re.fullmatch(r"[A-Za-z0-9_.-]+", key):
                raise CompileError(
                    f"invalid mapping key {key!r}",
                    SourceLocation(path, line.number, line.indent + 1),
                )
            if key in result:
                raise CompileError(
                    f"duplicate mapping key {key!r}",
                    SourceLocation(path, line.number, line.indent + 1),
                )
            value_text = value_text.strip()
            child_path = node_path + (key,)
            location = SourceLocation(
                path, line.number, line.indent + len(key_text) + 2
            )
            locations[child_path] = location
            if value_text:
                result[key] = _scalar(value_text, location)
                index += 1
                continue
            if index + 1 >= len(lines) or lines[index + 1].indent <= indent:
                result[key] = {}
                index += 1
                continue
            child, index = parse_block(index + 1, lines[index + 1].indent, child_path)
            result[key] = child
        return result, index

    if not lines:
        raise CompileError("document is empty", SourceLocation(path, 1, 1))
    if lines[0].indent:
        raise CompileError(
            "document root must not be indented", SourceLocation(path, 1, 1)
        )
    value, end = parse_block(0, 0, ())
    if end != len(lines) or not isinstance(value, dict):
        raise CompileError(
            "document root must be a mapping", SourceLocation(path, 1, 1)
        )
    return ParsedDocument(value, locations)


def _location(document: ParsedDocument, path: tuple[str | int, ...]) -> SourceLocation:
    while path:
        if path in document.locations:
            return document.locations[path]
        path = path[:-1]
    return SourceLocation(Path("<unknown>"), 1, 1)


def _source(
    document: ParsedDocument,
    path: tuple[str | int, ...],
    project_root: Path,
) -> dict[str, Any]:
    """Preserve stable authored provenance in normalized IR."""
    location = _location(document, path)
    try:
        source_path = location.path.resolve().relative_to(project_root.resolve()).as_posix()
    except ValueError as error:
        raise CompileError("interface imports must remain below the project directory", location) from error
    return {
        "file": source_path,
        "line": location.line,
        "column": location.column,
        "declaration_path": [str(part) for part in path],
    }


def _mapping(
    value: Any, name: str, document: ParsedDocument, path: tuple[str | int, ...]
) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise CompileError(f"{name} must be a mapping", _location(document, path))
    return value


def _string(
    value: Any, name: str, document: ParsedDocument, path: tuple[str | int, ...]
) -> str:
    if not isinstance(value, str) or not value:
        raise CompileError(
            f"{name} must be a non-empty string", _location(document, path)
        )
    return value


def _optional_string(
    value: Any, name: str, document: ParsedDocument, path: tuple[str | int, ...]
) -> str:
    if value is None:
        return ""
    if not isinstance(value, str):
        raise CompileError(f"{name} must be a string", _location(document, path))
    return value


def _boolean(
    value: Any, name: str, document: ParsedDocument, path: tuple[str | int, ...]
) -> bool:
    if not isinstance(value, bool):
        raise CompileError(f"{name} must be true or false", _location(document, path))
    return value


def _check_keys(
    value: dict[str, Any],
    allowed: set[str],
    name: str,
    document: ParsedDocument,
    path: tuple[str | int, ...],
) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        key = unknown[0]
        raise CompileError(
            f"unknown {name} key {key!r}", _location(document, path + (key,))
        )


def _stable_id(
    declaration: dict[str, Any],
    document: ParsedDocument,
    path: tuple[str | int, ...],
) -> int | None:
    value = declaration.get("id")
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int) or not 1 <= value <= 0xFFFFFFFF:
        raise CompileError(
            "id must be an integer from 1 through 4294967295",
            _location(document, path + ("id",)),
        )
    return value


def _version(
    declaration: dict[str, Any],
    document: ParsedDocument,
    path: tuple[str | int, ...],
) -> int:
    value = declaration.get("version", 1)
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise CompileError(
            "version must be a positive integer",
            _location(document, path + ("version",)),
        )
    return value


def _identifier(value: str, *, upper: bool = False) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not result or result[0].isdigit():
        result = "_" + result
    if keyword.iskeyword(result):
        result += "_"
    return result.upper() if upper else result


def _pascal(value: str) -> str:
    parts = [part for part in re.split(r"[^A-Za-z0-9]+", value) if part]
    result = "".join(part[:1].upper() + part[1:] for part in parts)
    return _identifier(result or "Generated")


PRIMITIVES: dict[str, tuple[str, str, int]] = {
    "bool": ("bool", "bool", 8),
    "u8": ("std::uint8_t", "unsigned", 8),
    "u16": ("std::uint16_t", "unsigned", 16),
    "u32": ("std::uint32_t", "unsigned", 32),
    "u64": ("std::uint64_t", "unsigned", 64),
    "i8": ("std::int8_t", "signed", 8),
    "i16": ("std::int16_t", "signed", 16),
    "i32": ("std::int32_t", "signed", 32),
    "i64": ("std::int64_t", "signed", 64),
    "f32": ("float", "float", 32),
    "f64": ("double", "float", 64),
}


def _fingerprint(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def _declaration_fingerprint(value: Any) -> str:
    """Fingerprint compatibility-relevant semantics, excluding identity/docs."""
    if isinstance(value, dict):
        normalized = {
            key: _declaration_fingerprint_value(item)
            for key, item in value.items()
            if key not in {"id", "renamed-from", "description", "unit"}
        }
    else:
        normalized = value
    return _fingerprint(normalized)


def _declaration_fingerprint_value(value: Any) -> Any:
    if isinstance(value, dict):
        return {
            key: _declaration_fingerprint_value(item)
            for key, item in value.items()
            if key not in {"id", "renamed-from", "description", "unit"}
        }
    if isinstance(value, list):
        return [_declaration_fingerprint_value(item) for item in value]
    return value


def _validate_python_paths(collection: str, names: Iterable[str]) -> None:
    normalized: dict[tuple[str, ...], str] = {}
    leaves: set[tuple[str, ...]] = set()
    for name in names:
        parts = tuple(_identifier(part) for part in name.split("."))
        if any(not part for part in parts):
            raise CompileError(f"{collection} name {name!r} has an empty path segment")
        previous = normalized.get(parts)
        if previous is not None:
            raise CompileError(
                f"{collection} names {previous!r} and {name!r} produce the same Python path"
            )
        for size in range(1, len(parts)):
            if parts[:size] in leaves:
                raise CompileError(
                    f"{collection} name {name!r} extends an endpoint path"
                )
        if any(existing[: len(parts)] == parts for existing in leaves):
            raise CompileError(f"{collection} name {name!r} shadows a namespace")
        normalized[parts] = name
        leaves.add(parts)


@dataclass(frozen=True, slots=True)
class AuthoredProject:
    path: Path
    project: ParsedDocument
    interfaces: tuple[tuple[Path, ParsedDocument], ...]


def load_authored_project(project_path: Path) -> AuthoredProject:
    """Frontend stage: load all authored files and retain syntax provenance."""
    project_path = project_path.resolve()
    project_document = parse_strict_yaml(project_path)
    interfaces = project_document.value.get("interfaces")
    if not isinstance(interfaces, list) or not interfaces:
        raise CompileError(
            "interfaces must be a non-empty sequence",
            _location(project_document, ("interfaces",)),
        )
    loaded: list[tuple[Path, ParsedDocument]] = []
    for index, relative in enumerate(interfaces):
        if not isinstance(relative, str):
            raise CompileError(
                "interface path must be a string",
                _location(project_document, ("interfaces", index)),
            )
        path = (project_path.parent / relative).resolve()
        try:
            path.relative_to(project_path.parent)
        except ValueError as error:
            raise CompileError(
                "interface imports must remain below the project directory",
                _location(project_document, ("interfaces", index)),
            ) from error
        loaded.append((path, parse_strict_yaml(path)))
    return AuthoredProject(project_path, project_document, tuple(loaded))


def compile_project(
    project_path: Path, lock_path: Path | None = None
) -> tuple[dict, dict, dict]:
    authored = load_authored_project(project_path)
    project_path = authored.path
    project_document = authored.project
    project = project_document.value
    _check_keys(
        project,
        {"solar", "application", "interfaces", "generation", "modules"},
        "project",
        project_document,
        (),
    )
    if project.get("solar") != PROJECT_FORMAT:
        raise CompileError(
            "project manifest requires 'solar: 1'",
            _location(project_document, ("solar",)),
        )
    application = _mapping(
        project.get("application"), "application", project_document, ("application",)
    )
    _check_keys(
        application,
        {"name", "namespace"},
        "application",
        project_document,
        ("application",),
    )
    app_name = _string(
        application.get("name"),
        "application.name",
        project_document,
        ("application", "name"),
    )
    namespace = _string(
        application.get("namespace"),
        "application.namespace",
        project_document,
        ("application", "namespace"),
    )
    interfaces = project.get("interfaces")
    if not isinstance(interfaces, list) or not interfaces:
        raise CompileError(
            "interfaces must be a non-empty sequence",
            _location(project_document, ("interfaces",)),
        )

    previous = {"format": LOCK_VERSION, "entries": {}, "retired": {}}
    if lock_path is not None and lock_path.exists():
        previous = json.loads(lock_path.read_text())
        if previous.get("format") != LOCK_VERSION:
            raise CompileError("unsupported interface lock format")
    allocator = IdentityAllocator(previous)

    raw_types: dict[
        str, tuple[dict[str, Any], ParsedDocument, tuple[str | int, ...], str]
    ] = {}
    raw_parameters: dict[
        str, tuple[dict[str, Any], ParsedDocument, tuple[str | int, ...], str]
    ] = {}
    raw_data: dict[
        str, tuple[dict[str, Any], ParsedDocument, tuple[str | int, ...], str]
    ] = {}
    raw_actions: dict[
        str, tuple[dict[str, Any], ParsedDocument, tuple[str | int, ...], str]
    ] = {}
    raw_streams: dict[
        str, tuple[dict[str, Any], ParsedDocument, tuple[str | int, ...], str]
    ] = {}
    raw_stream_groups: dict[
        str, tuple[dict[str, Any], ParsedDocument, tuple[str | int, ...], str]
    ] = {}
    project_root = project_path.parent.resolve()
    dependencies = [project_path.name]

    def collect(
        target: dict,
        source: dict,
        category: str,
        document: ParsedDocument,
        package: str,
    ):
        for name, declaration in source.items():
            if name in target:
                raise CompileError(
                    f"duplicate {category} declaration {name!r}",
                    _location(document, (category, name)),
                )
            target[name] = (
                _mapping(declaration, f"{category}.{name}", document, (category, name)),
                document,
                (category, name),
                package,
            )

    for interface_path, document in authored.interfaces:
        dependencies.append(interface_path.relative_to(project_root).as_posix())
        _check_keys(
            document.value,
            {"solar-interface", "package", "types", "parameters", "data", "actions", "stream-groups", "streams"},
            "interface",
            document,
            (),
        )
        if document.value.get("solar-interface") != INTERFACE_FORMAT:
            raise CompileError(
                "interface requires 'solar-interface: 1'",
                _location(document, ("solar-interface",)),
            )
        package = _string(
            document.value.get("package"), "package", document, ("package",)
        )
        collect(
            raw_types,
            _mapping(document.value.get("types", {}), "types", document, ("types",)),
            "types",
            document,
            package,
        )
        collect(
            raw_parameters,
            _mapping(
                document.value.get("parameters", {}),
                "parameters",
                document,
                ("parameters",),
            ),
            "parameters",
            document,
            package,
        )
        collect(
            raw_data,
            _mapping(document.value.get("data", {}), "data", document, ("data",)),
            "data",
            document,
            package,
        )
        collect(
            raw_actions,
            _mapping(
                document.value.get("actions", {}), "actions", document, ("actions",)
            ),
            "actions",
            document,
            package,
        )
        collect(
            raw_streams,
            _mapping(
                document.value.get("streams", {}), "streams", document, ("streams",)
            ),
            "streams",
            document,
            package,
        )
        collect(
            raw_stream_groups,
            _mapping(
                document.value.get("stream-groups", {}),
                "stream-groups",
                document,
                ("stream-groups",),
            ),
            "stream-groups",
            document,
            package,
        )

    generation = _mapping(
        project.get("generation", {}), "generation", project_document, ("generation",)
    )
    _check_keys(
        generation,
        {"maximum-types", "maximum-parameters", "maximum-data", "maximum-actions", "maximum-stream-groups", "maximum-streams"},
        "generation",
        project_document,
        ("generation",),
    )
    limits = {
        "types": generation.get("maximum-types", 64),
        "parameters": generation.get("maximum-parameters", 128),
        "data": generation.get("maximum-data", 128),
        "actions": generation.get("maximum-actions", 128),
        "stream-groups": generation.get("maximum-stream-groups", 32),
        "streams": generation.get("maximum-streams", 128),
    }
    declarations = {
        "types": raw_types,
        "parameters": raw_parameters,
        "data": raw_data,
        "actions": raw_actions,
        "stream-groups": raw_stream_groups,
        "streams": raw_streams,
    }
    for category, limit in limits.items():
        if not isinstance(limit, int) or isinstance(limit, bool) or limit < 0:
            raise CompileError(
                f"generation.maximum-{category} must be a non-negative integer",
                _location(project_document, ("generation", f"maximum-{category}")),
            )
        if len(declarations[category]) > limit:
            raise CompileError(
                f"{category} declaration count {len(declarations[category])} exceeds configured maximum {limit}",
                _location(project_document, ("generation", f"maximum-{category}")),
            )

    types: dict[str, dict[str, Any]] = {}
    for name, (declaration, document, path, package) in raw_types.items():
        kind = declaration.get("kind")
        if kind not in ("struct", "enum"):
            raise CompileError(
                "type kind must be 'struct' or 'enum'",
                _location(document, path + ("kind",)),
            )
        common_type_keys = {"kind", "id", "version", "renamed-from", "description"}
        _check_keys(
            declaration,
            common_type_keys
            | ({"values", "underlying", "open"} if kind == "enum" else {"fields"}),
            f"type {name}",
            document,
            path,
        )
        version = _version(declaration, document, path)
        qualified = f"{package}.{name}"
        stable_id, _ = allocator.allocate(
            f"schema:{qualified}",
            _declaration_fingerprint(declaration),
            explicit=_stable_id(declaration, document, path),
            renamed_from=(
                f"schema:{package}.{declaration['renamed-from']}"
                if isinstance(declaration.get("renamed-from"), str)
                else None
            ),
        )
        item = {
            "id": stable_id,
            "name": qualified,
            "cpp_name": _identifier(name),
            "kind": kind,
            "description": _optional_string(
                declaration.get("description"), "type description", document,
                path + ("description",)
            ),
            "version": version,
            "source": _source(document, path, project_root),
        }
        if kind == "enum":
            values = _mapping(
                declaration.get("values"),
                f"enum {name} values",
                document,
                path + ("values",),
            )
            if not values:
                raise CompileError(
                    "enum requires at least one value",
                    _location(document, path + ("values",)),
                )
            if not all(isinstance(value, int) and not isinstance(value, bool)
                       for value in values.values()) or len(
                set(values.values())
            ) != len(values):
                raise CompileError(
                    "enum values must be unique integers",
                    _location(document, path + ("values",)),
                )
            item.update(
                {
                    "underlying": declaration.get("underlying", "u8"),
                    "open": _boolean(
                        declaration.get("open", False), "enum open", document,
                        path + ("open",)
                    ),
                    "values": values,
                }
            )
            underlying = item["underlying"]
            if underlying in PRIMITIVES:
                _, numeric_kind, width = PRIMITIVES[underlying]
                if numeric_kind in ("unsigned", "signed"):
                    minimum = 0 if numeric_kind == "unsigned" else -(2 ** (width - 1))
                    maximum = 2**width - 1 if numeric_kind == "unsigned" else 2 ** (width - 1) - 1
                    if any(not minimum <= value <= maximum for value in values.values()):
                        raise CompileError(
                            f"enum value does not fit underlying type {underlying}",
                            _location(document, path + ("values",)),
                        )
        else:
            fields = _mapping(
                declaration.get("fields", {}),
                f"struct {name} fields",
                document,
                path + ("fields",),
            )
            parsed_fields = []
            used_field_ids: dict[int, str] = {}
            for field_name, raw_field in fields.items():
                field_decl = (
                    raw_field if isinstance(raw_field, dict) else {"type": raw_field}
                )
                _check_keys(
                    field_decl,
                    {"id", "renamed-from", "type", "optional", "unit", "description"},
                    f"field {name}.{field_name}",
                    document,
                    path + ("fields", field_name),
                )
                field_type = field_decl.get("type")
                if not isinstance(field_type, str):
                    raise CompileError(
                        "field type must be a string",
                        _location(document, path + ("fields", field_name)),
                    )
                optional = _boolean(
                    field_decl.get("optional", False), "field optional", document,
                    path + ("fields", field_name, "optional"),
                )
                if optional and not field_type.startswith(
                    "optional<"
                ):
                    field_type = f"optional<{field_type}>"
                explicit_field_id = field_decl.get("id")
                if explicit_field_id is not None and (
                    isinstance(explicit_field_id, bool) or
                    not isinstance(explicit_field_id, int) or
                    not 1 <= explicit_field_id <= 0xFFFF
                ):
                    raise CompileError(
                        "field id must be an integer from 1 through 65535",
                        _location(document, path + ("fields", field_name, "id")),
                    )
                renamed_field = field_decl.get("renamed-from")
                if renamed_field is not None and not isinstance(renamed_field, str):
                    raise CompileError(
                        "field renamed-from must be a string",
                        _location(document, path + ("fields", field_name, "renamed-from")),
                    )
                field_id = allocator.allocate_scoped(
                    f"field:{qualified}", field_name, _declaration_fingerprint(field_decl),
                    explicit=explicit_field_id, renamed_from=renamed_field, maximum=0xFFFF,
                )
                if field_id in used_field_ids:
                    raise CompileError(
                        f"field id {field_id} is shared by {used_field_ids[field_id]!r} and "
                        f"{field_name!r}",
                        _location(document, path + ("fields", field_name, "id")),
                    )
                used_field_ids[field_id] = field_name
                parsed_fields.append(
                    {
                        "id": field_id,
                        "name": field_name,
                        "type": field_type,
                        "required": not optional,
                        "unit": _optional_string(
                            field_decl.get("unit"), "field unit", document,
                            path + ("fields", field_name, "unit"),
                        ),
                        "description": _optional_string(
                            field_decl.get("description"), "field description", document,
                            path + ("fields", field_name, "description"),
                        ),
                        "source": _source(
                            document, path + ("fields", field_name), project_root
                        ),
                    }
                )
            item["fields"] = parsed_fields
        types[name] = item

    def resolve_type(
        name: str, document: ParsedDocument, path: tuple[str | int, ...]
    ) -> dict[str, Any]:
        if name in PRIMITIVES:
            cpp, kind, width = PRIMITIVES[name]
            return {
                "name": name,
                "cpp": cpp,
                "kind": kind,
                "width": width,
                "schema": None,
                "maximum_length": 0,
            }
        if name in types:
            return {
                "name": name,
                "cpp": types[name]["cpp_name"],
                "kind": "schema",
                "width": 0,
                "schema": types[name]["id"],
                "maximum_length": 0,
            }
        bounded = re.fullmatch(r"(string|bytes)<([1-9][0-9]*)>", name)
        if bounded:
            capacity = int(bounded.group(2))
            if capacity > 0xFFFF:
                raise CompileError(
                    "bounded string/bytes capacity must not exceed 65535",
                    _location(document, path),
                )
            kind = "text" if bounded.group(1) == "string" else "bytes"
            cpp = "solar::BoundedText" if kind == "text" else "solar::BoundedBytes"
            return {
                "name": name,
                "cpp": f"{cpp}<{capacity}>",
                "kind": kind,
                "width": 0,
                "schema": None,
                "maximum_length": capacity,
            }
        optional = re.fullmatch(r"optional<(.+)>", name)
        if optional:
            value = resolve_type(optional.group(1).strip(), document, path)
            return {
                "name": name,
                "cpp": f"std::optional<{value['cpp']}>",
                "kind": "optional",
                "width": value["width"],
                "schema": value["schema"],
                "maximum_length": value["maximum_length"],
                "element": value,
            }
        collection = re.fullmatch(r"(array|sequence)<(.+),\s*([1-9][0-9]*)>", name)
        if collection:
            value = resolve_type(collection.group(2).strip(), document, path)
            capacity = int(collection.group(3))
            if capacity > 0xFFFF:
                raise CompileError(
                    "collection capacity must not exceed 65535", _location(document, path)
                )
            cpp = (
                f"std::array<{value['cpp']}, {capacity}>"
                if collection.group(1) == "array"
                else f"solar::BoundedVector<{value['cpp']}, {capacity}>"
            )
            return {
                "name": name,
                "cpp": cpp,
                "kind": collection.group(1),
                "width": 0,
                "schema": None,
                "maximum_length": capacity,
                "element": value,
            }
        raise CompileError(f"unknown type {name!r}", _location(document, path))

    def leaf_type(resolved: dict[str, Any]) -> dict[str, Any]:
        while "element" in resolved:
            resolved = resolved["element"]
        return resolved

    def encoded_bound(resolved: dict[str, Any]) -> int:
        kind = resolved["kind"]
        if kind in ("optional",):
            return 1 + encoded_bound(resolved["element"])
        if kind in ("array", "sequence"):
            return 5 + resolved["maximum_length"] * encoded_bound(resolved["element"])
        if kind in ("text", "bytes"):
            return 5 + resolved["maximum_length"]
        if kind == "schema":
            return schema_encoded_bound(id_to_type[resolved["schema"]])
        return max(1, resolved["width"] // 8) + 1

    # Resolve the complete type graph before any backend-facing schema is
    # formed. This makes recursion and unbounded layouts semantic errors rather
    # than backend-specific failures.
    id_to_type = {item["id"]: name for name, item in types.items()}
    for name, item in types.items():
        if item["kind"] != "struct":
            continue
        _, document, path, _ = raw_types[name]
        for field in item["fields"]:
            field["resolved"] = resolve_type(
                field["type"], document, path + ("fields", field["name"], "type")
            )

    def validate_acyclic(name: str, path_names: tuple[str, ...]) -> None:
        if name in path_names:
            _, document, path, _ = raw_types[name]
            chain = " -> ".join((*path_names, name))
            raise CompileError(
                f"recursive firmware type layout is forbidden: {chain}",
                _location(document, path),
            )
        item = types[name]
        if item["kind"] != "struct":
            return
        for field in item["fields"]:
            leaf = leaf_type(field["resolved"])
            if leaf["schema"] is not None:
                validate_acyclic(id_to_type[leaf["schema"]], (*path_names, name))

    for type_name in types:
        validate_acyclic(type_name, ())

    encoded_bounds: dict[str, int] = {}

    def schema_encoded_bound(name: str) -> int:
        if name in encoded_bounds:
            return encoded_bounds[name]
        item = types[name]
        if item["kind"] == "enum":
            result = max(1, PRIMITIVES[item["underlying"]][2] // 8) + 1
        else:
            result = 2 + sum(5 + encoded_bound(field["resolved"])
                             for field in item["fields"])
        encoded_bounds[name] = result
        return result

    schemas: list[dict[str, Any]] = []
    for name, item in types.items():
        if item["kind"] == "enum":
            if item["underlying"] not in PRIMITIVES or PRIMITIVES[item["underlying"]][
                1
            ] not in ("unsigned", "signed"):
                declaration, document, path, _ = raw_types[name]
                raise CompileError(
                    "enum underlying type must be an integer",
                    _location(document, path + ("underlying",)),
                )
            _, underlying_kind, width = PRIMITIVES[item["underlying"]]
            schemas.append(
                {
                    "id": item["id"],
                    "name": item["name"],
                    "description": item["description"],
                    "version": item["version"],
                    "shape": "enumeration",
                    "codec": "none",
                    "max_encoded_size": 0,
                    "underlying_kind": underlying_kind,
                    "underlying_width": width,
                    "open": item["open"],
                    "values": [
                        {
                            "name": key,
                            "value": value,
                            "description": "",
                            "deprecated": False,
                        }
                        for key, value in item["values"].items()
                    ],
                    "fields": [],
                }
            )
            continue
        declaration, document, path, _ = raw_types[name]
        fields = []
        maximum = 2
        for field in item["fields"]:
            resolved = field["resolved"]
            maximum += encoded_bound(resolved) + 5
            manifest_type = leaf_type(resolved)
            if manifest_type["schema"] is not None:
                referenced = types[id_to_type[manifest_type["schema"]]]
                if referenced["kind"] == "enum":
                    _, _, enum_width = PRIMITIVES[referenced["underlying"]]
                    manifest_type = {
                        **manifest_type,
                        "kind": "enum",
                        "width": enum_width,
                    }
            fields.append(
                {
                    "id": field["id"],
                    "name": field["name"],
                    "description": field["description"],
                    "unit": field["unit"],
                    "kind": manifest_type["kind"],
                    "required": field["required"] and resolved["kind"] != "optional",
                    "deprecated": False,
                    "width": manifest_type["width"],
                    "maximum_length": resolved["maximum_length"],
                    "schema": manifest_type["schema"],
                    "packed_offset": None,
                }
            )
        schemas.append(
            {
                "id": item["id"],
                "name": item["name"],
                "description": item["description"],
                "version": item["version"],
                "shape": "object",
                "codec": "cbor",
                "max_encoded_size": maximum,
                "underlying_kind": None,
                "underlying_width": 0,
                "open": False,
                "values": [],
                "fields": fields,
            }
        )

    parameters = []
    data = []
    capabilities = []
    for name, (declaration, document, path, package) in raw_parameters.items():
        _check_keys(
            declaration,
            {"id", "version", "renamed-from", "type", "default", "minimum", "maximum",
             "unit", "description"},
            f"parameter {name}",
            document,
            path,
        )
        version = _version(declaration, document, path)
        description = _optional_string(
            declaration.get("description"), "parameter description", document,
            path + ("description",),
        )
        unit = _optional_string(
            declaration.get("unit"), "parameter unit", document, path + ("unit",)
        )
        type_name = _string(
            declaration.get("type"), "parameter type", document, path + ("type",)
        )
        resolved = resolve_type(type_name, document, path + ("type",))
        if resolved["kind"] not in ("bool", "unsigned", "signed", "float"):
            raise CompileError(
                "parameters currently require a boolean or numeric scalar type",
                _location(document, path + ("type",)),
            )
        if "default" not in declaration:
            raise CompileError(
                "parameter requires a default", _location(document, path)
            )
        default = declaration["default"]
        minimum = declaration.get("minimum")
        maximum = declaration.get("maximum")
        numeric_kind = resolved["kind"] in ("unsigned", "signed", "float")
        expected_python_type = float if resolved["kind"] == "float" else int
        if numeric_kind:
            if isinstance(default, bool) or not isinstance(
                default, (int, float) if expected_python_type is float else int
            ):
                raise CompileError(
                    "parameter default has an incompatible type",
                    _location(document, path + ("default",)),
                )
            for bound_name, bound in (("minimum", minimum), ("maximum", maximum)):
                if bound is not None and (
                    isinstance(bound, bool)
                    or not isinstance(
                        bound, (int, float) if expected_python_type is float else int
                    )
                ):
                    raise CompileError(
                        f"parameter {bound_name} has an incompatible type",
                        _location(document, path + (bound_name,)),
                    )
        elif not isinstance(default, bool):
            raise CompileError(
                "parameter default has an incompatible type",
                _location(document, path + ("default",)),
            )
        if not numeric_kind and (minimum is not None or maximum is not None):
            raise CompileError(
                "boolean parameters cannot declare numeric bounds",
                _location(document, path),
            )
        if minimum is not None and maximum is not None and minimum > maximum:
            raise CompileError(
                "parameter minimum must not exceed maximum",
                _location(document, path + ("minimum",)),
            )
        if (
            minimum is not None
            and default < minimum
            or maximum is not None
            and default > maximum
        ):
            raise CompileError(
                "parameter default violates declared bounds",
                _location(document, path + ("default",)),
            )
        stable_id, _ = allocator.allocate(
            f"parameter:{name}",
            _declaration_fingerprint(declaration),
            explicit=_stable_id(declaration, document, path),
            renamed_from=(
                f"parameter:{declaration['renamed-from']}"
                if isinstance(declaration.get("renamed-from"), str)
                else None
            ),
        )
        wrapper_name = _pascal(name) + "Value"
        schema_name = f"{package}.{wrapper_name}"
        schema_id, _ = allocator.allocate(
            f"schema:{schema_name}",
            _fingerprint({"value": type_name}),
            renamed_from=(
                f"schema:{package}.{_pascal(declaration['renamed-from'])}Value"
                if isinstance(declaration.get("renamed-from"), str)
                else None
            ),
        )
        schemas.append(
            {
                "id": schema_id,
                "name": schema_name,
                "description": description,
                "version": 1,
                "shape": "object",
                "codec": "cbor",
                "max_encoded_size": max(8, resolved["width"] // 8 + 5),
                "underlying_kind": None,
                "underlying_width": 0,
                "open": False,
                "values": [],
                "fields": [
                    {
                        "id": 1,
                        "name": "value",
                        "description": description,
                        "unit": unit,
                        "kind": resolved["kind"],
                        "required": True,
                        "deprecated": False,
                        "width": resolved["width"],
                        "maximum_length": 0,
                        "schema": None,
                        "packed_offset": None,
                    }
                ],
            }
        )
        item = {
            "id": stable_id,
            "name": name,
            "cpp_name": _pascal(name),
            "type": resolved,
            "default": default,
            "minimum": minimum,
            "maximum": maximum,
            "schema": schema_id,
            "wrapper": wrapper_name,
            "version": version,
            "source": _source(document, path, project_root),
            "unit": unit,
            "description": description,
        }
        parameters.append(item)
        data.append(
            {
                "id": stable_id,
                "name": name,
                "description": description,
                "version": version,
                "schema": schema_id,
                "capability_mask": 3,
            }
        )
        for kind in ("query", "update"):
            capabilities.append(
                {
                    "domain": "data",
                    "kind": kind,
                    "endpoint": stable_id,
                    "permission_mask": 1 if kind == "query" else 2,
                    "codec": "cbor",
                    "maximum_rate_hz": 0,
                    "maximum_batch": 1,
                    "reliable_window": 0,
                    "delivery": "none",
                    "cancellation": True,
                    "batched": False,
                    "explicit_open": False,
                    "on_open": False,
                    "on_close": False,
                    "exclusive": False,
                    "replacement": "none",
                    "group": None,
                }
            )

    declared_data = []
    for name, (declaration, document, path, _) in raw_data.items():
        _check_keys(
            declaration,
            {"id", "version", "renamed-from", "type", "access", "description"},
            f"data {name}",
            document,
            path,
        )
        version = _version(declaration, document, path)
        description = _optional_string(
            declaration.get("description"),
            "data description",
            document,
            path + ("description",),
        )
        type_name = _string(
            declaration.get("type"), "data type", document, path + ("type",)
        )
        resolved = resolve_type(type_name, document, path + ("type",))
        if resolved["schema"] is None:
            raise CompileError(
                "data values must use a struct schema",
                _location(document, path + ("type",)),
            )
        access = declaration.get("access", ["query"])
        if (
            not isinstance(access, list)
            or not access
            or any(item not in ("query", "update") for item in access)
            or len(set(access)) != len(access)
        ):
            raise CompileError(
                "data access must be a non-empty unique sequence of 'query' and/or 'update'",
                _location(document, path + ("access",)),
            )
        stable_id, _ = allocator.allocate(
            f"data:{name}",
            _declaration_fingerprint(declaration),
            explicit=_stable_id(declaration, document, path),
            renamed_from=(
                f"data:{declaration['renamed-from']}"
                if isinstance(declaration.get("renamed-from"), str)
                else None
            ),
        )
        item = {
            "id": stable_id,
            "name": name,
            "cpp_name": _pascal(name) + "Data",
            "type": resolved,
            "schema": resolved["schema"],
            "query": "query" in access,
            "update": "update" in access,
            "description": description,
            "version": version,
            "source": _source(document, path, project_root),
        }
        declared_data.append(item)
        capability_mask = (1 if item["query"] else 0) | (2 if item["update"] else 0)
        data.append(
            {
                "id": stable_id,
                "name": name,
                "description": description,
                "version": version,
                "schema": item["schema"],
                "capability_mask": capability_mask,
            }
        )
        for kind in access:
            capabilities.append(
                {
                    "domain": "data",
                    "kind": kind,
                    "endpoint": stable_id,
                    "permission_mask": 1 if kind == "query" else 2,
                    "codec": "cbor",
                    "maximum_rate_hz": 0,
                    "maximum_batch": 1,
                    "reliable_window": 0,
                    "delivery": "none",
                    "cancellation": True,
                    "batched": False,
                    "explicit_open": False,
                    "on_open": False,
                    "on_close": False,
                    "exclusive": False,
                    "replacement": "none",
                    "group": None,
                }
            )

    actions = []
    for name, (declaration, document, path, _) in raw_actions.items():
        _check_keys(
            declaration,
            {"id", "version", "renamed-from", "request", "response", "description"},
            f"action {name}",
            document,
            path,
        )
        version = _version(declaration, document, path)
        description = _optional_string(
            declaration.get("description"), "action description", document,
            path + ("description",),
        )
        request = _string(
            declaration.get("request"), "action request", document, path + ("request",)
        )
        response = _string(
            declaration.get("response"),
            "action response",
            document,
            path + ("response",),
        )
        request_type = resolve_type(request, document, path + ("request",))
        response_type = resolve_type(response, document, path + ("response",))
        if request_type["schema"] is None or response_type["schema"] is None:
            raise CompileError(
                "action request and response must be struct schemas",
                _location(document, path),
            )
        stable_id, _ = allocator.allocate(
            f"action:{name}",
            _declaration_fingerprint(declaration),
            explicit=_stable_id(declaration, document, path),
            renamed_from=(
                f"action:{declaration['renamed-from']}"
                if isinstance(declaration.get("renamed-from"), str)
                else None
            ),
        )
        actions.append(
            {
                "id": stable_id,
                "name": name,
                "cpp_name": _pascal(name) + "Action",
                "request": request_type,
                "response": response_type,
                "request_schema": request_type["schema"],
                "response_schema": response_type["schema"],
                "error_schema": 3,
                "description": description,
                "version": version,
                "permission_mask": 0,
                "source": _source(document, path, project_root),
            }
        )

    stream_groups = []
    stream_groups_by_name = {}
    for name, (declaration, document, path, _) in raw_stream_groups.items():
        _check_keys(
            declaration,
            {"id", "version", "renamed-from", "replacement", "description"},
            f"stream group {name}",
            document,
            path,
        )
        replacement = declaration.get("replacement", "replace")
        if replacement not in ("replace", "reject"):
            raise CompileError(
                "stream group replacement must be 'replace' or 'reject'",
                _location(document, path + ("replacement",)),
            )
        stable_id, _ = allocator.allocate(
            f"stream-group:{name}",
            _declaration_fingerprint(declaration),
            explicit=_stable_id(declaration, document, path),
            renamed_from=(
                f"stream-group:{declaration['renamed-from']}"
                if isinstance(declaration.get("renamed-from"), str)
                else None
            ),
        )
        item = {
            "id": stable_id,
            "name": name,
            "cpp_name": _pascal(name) + "StreamGroup",
            "description": _optional_string(
                declaration.get("description"),
                "stream group description",
                document,
                path + ("description",),
            ),
            "replacement": replacement,
            "version": _version(declaration, document, path),
            "source": _source(document, path, project_root),
        }
        stream_groups.append(item)
        stream_groups_by_name[name] = item

    streams = []
    output_streams = []
    for name, (declaration, document, path, _) in raw_streams.items():
        _check_keys(
            declaration,
            {"id", "version", "renamed-from", "type", "direction", "maximum-rate", "exclusive-group",
             "description"},
            f"stream {name}",
            document,
            path,
        )
        version = _version(declaration, document, path)
        description = _optional_string(
            declaration.get("description"), "stream description", document,
            path + ("description",),
        )
        type_name = _string(
            declaration.get("type"), "stream type", document, path + ("type",)
        )
        resolved = resolve_type(type_name, document, path + ("type",))
        if resolved["schema"] is None:
            raise CompileError(
                "stream values must use a struct schema",
                _location(document, path + ("type",)),
            )
        direction = declaration.get("direction")
        if direction not in ("in", "out"):
            raise CompileError(
                "stream direction must be 'in' or 'out'",
                _location(document, path + ("direction",)),
            )
        exclusive_group_name = declaration.get("exclusive-group")
        if exclusive_group_name is not None:
            if direction != "in":
                raise CompileError(
                    "only input streams may declare an exclusive group",
                    _location(document, path + ("exclusive-group",)),
                )
            exclusive_group_name = _string(
                exclusive_group_name,
                "stream exclusive-group",
                document,
                path + ("exclusive-group",),
            )
            if exclusive_group_name not in stream_groups_by_name:
                raise CompileError(
                    f"unknown stream group {exclusive_group_name!r}",
                    _location(document, path + ("exclusive-group",)),
                )
        stable_id, _ = allocator.allocate(
            f"stream:{name}",
            _declaration_fingerprint(declaration),
            explicit=_stable_id(declaration, document, path),
            renamed_from=(
                f"stream:{declaration['renamed-from']}"
                if isinstance(declaration.get("renamed-from"), str)
                else None
            ),
        )
        maximum_rate = declaration.get("maximum-rate", 100)
        if (isinstance(maximum_rate, bool) or not isinstance(maximum_rate, int) or
                not 1 <= maximum_rate <= 0xFFFFFFFF):
            raise CompileError(
                "stream maximum-rate must be an integer from 1 through 4294967295",
                _location(document, path + ("maximum-rate",)),
            )
        item = {
            "id": stable_id,
            "name": name,
            "cpp_name": _pascal(name) + "Stream",
            "type": resolved,
            "schema": resolved["schema"],
            "direction": direction,
            "maximum_rate_hz": maximum_rate,
            "exclusive_group": stream_groups_by_name.get(exclusive_group_name),
            "description": description,
            "version": version,
            "codec": "cbor",
            "source": _source(document, path, project_root),
        }
        streams.append(item)
        if direction == "out":
            output_streams.append(
                {
                    "id": stable_id,
                    "name": name,
                    "description": description,
                    "version": version,
                    "schema": item["schema"],
                    "codec": "cbor",
                }
            )
            capabilities.append(
                {
                    "domain": "stream",
                    "kind": "out_stream",
                    "endpoint": stable_id,
                    "permission_mask": 1,
                    "codec": "cbor",
                    # A Stream is producer paced. The declared maximum remains
                    # in the generated C++ declaration and client validation;
                    # the Stream publication capability itself is not a poll
                    # schedule.
                    "maximum_rate_hz": 0,
                    "maximum_batch": 1,
                    "reliable_window": 0,
                    "delivery": "latest",
                    "cancellation": False,
                    "batched": False,
                    "explicit_open": False,
                    "on_open": False,
                    "on_close": False,
                    "exclusive": False,
                    "replacement": "none",
                    "group": None,
                }
            )
        else:
            output_streams.append(
                {
                    "id": stable_id,
                    "name": name,
                    "description": description,
                    "version": version,
                    "schema": item["schema"],
                    "codec": "cbor",
                }
            )
            capabilities.append(
                {
                    "domain": "stream",
                    "kind": "in_stream",
                    "endpoint": stable_id,
                    "permission_mask": 4,
                    "codec": "cbor",
                    "maximum_rate_hz": maximum_rate,
                    "maximum_batch": 1,
                    "reliable_window": 4,
                    "delivery": "reliable",
                    "cancellation": True,
                    "batched": False,
                    "explicit_open": True,
                    "on_open": True,
                    "on_close": True,
                    "exclusive": exclusive_group_name is not None,
                    "replacement": (
                        stream_groups_by_name[exclusive_group_name]["replacement"]
                        if exclusive_group_name is not None
                        else "none"
                    ),
                    "group": (
                        stream_groups_by_name[exclusive_group_name]["id"]
                        if exclusive_group_name is not None
                        else None
                    ),
                }
            )

    lock, changes = allocator.finish()
    _validate_python_paths("parameter", (item["name"] for item in parameters))
    _validate_python_paths("data", (item["name"] for item in declared_data))
    _validate_python_paths("action", (item["name"] for item in actions))
    _validate_python_paths("stream", (item["name"] for item in streams))
    python_package = _identifier(app_name.replace("-", "_") + "_solar")
    ir = {
        "format": IR_VERSION,
        "generator": GENERATOR_VERSION,
        "application": {
            "name": app_name,
            "namespace": namespace,
            "python_package": python_package,
        },
        "limits": limits,
        "types": list(types.values()),
        "parameters": sorted(parameters, key=lambda item: item["id"]),
        "data_declarations": sorted(declared_data, key=lambda item: item["id"]),
        "actions": sorted(actions, key=lambda item: item["id"]),
        "stream_declarations": sorted(streams, key=lambda item: item["id"]),
        "stream_groups": sorted(stream_groups, key=lambda item: item["id"]),
        "manifest": {
            "format": EFFECTIVE_MANIFEST_FORMAT,
            "protocol": list(REMOTE_PROTOCOL),
            "schemas": sorted(schemas, key=lambda item: item["id"]),
            "data": sorted(data, key=lambda item: item["id"]),
            "actions": sorted(actions, key=lambda item: item["id"]),
            "topics": [],
            "streams": sorted(output_streams, key=lambda item: item["id"]),
            "links": [],
            "capabilities": sorted(
                capabilities,
                key=lambda item: (item["domain"], item["endpoint"], item["kind"]),
            ),
            "in_stream_groups": sorted(
                [
                    {
                        "id": item["id"],
                        "name": item["name"],
                        "description": item["description"],
                    }
                    for item in stream_groups
                ],
                key=lambda item: item["id"],
            ),
        },
        "dependencies": dependencies,
    }
    return ir, lock, {"format": COMPATIBILITY_FORMAT, "changes": changes}


def _cpp_literal(value: Any, cpp_type: str) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        suffix = "F" if cpp_type == "float" else ""
        rendered = f"{value:.9g}"
        if "." not in rendered and "e" not in rendered.lower():
            rendered += ".0"
        return rendered + suffix
    return str(value)


def _generated_header(*includes: str) -> list[str]:
    return [
        "// Generated by Solar codegen; do not edit.",
        "#pragma once",
        "",
        *(f"#include <{include}>" for include in includes),
        "",
    ]


def generate_types_cpp(ir: dict[str, Any]) -> str:
    namespace = ir["application"]["namespace"]
    lines = _generated_header(
        "array", "cstdint", "optional", "solar/core/bounded.hpp"
    )
    lines.extend([f"namespace {namespace}::generated", "{", ""])
    for item in ir["types"]:
        if item["kind"] == "enum":
            underlying = PRIMITIVES[item["underlying"]][0]
            lines.append(f"enum class {item['cpp_name']} : {underlying}")
            lines.append("{")
            for name, value in item["values"].items():
                lines.append(f"    {_identifier(name, upper=True)} = {value},")
            lines.extend(["};", ""])
        else:
            lines.append(f"struct {item['cpp_name']}")
            lines.append("{")
            for field in item["fields"]:
                cpp = field["resolved"]["cpp"]
                lines.append(f"    {cpp} {_identifier(field['name'])}{{}};")
            lines.extend(["};", ""])
    lines.extend([f"}} // namespace {namespace}::generated", ""])
    return "\n".join(lines)


def generate_parameters_cpp(ir: dict[str, Any]) -> str:
    namespace = ir["application"]["namespace"]
    lines = _generated_header(
        "cstdint", "solar/generated/types.hpp", "solar/parameters/store.hpp"
    )
    lines.extend([f"namespace {namespace}::generated", "{", ""])
    for item in ir["parameters"]:
        cpp = item["type"]["cpp"]
        lines.extend(
            [
                f"struct {item['cpp_name']}",
                "{",
                f"    using Value = {cpp};",
                f'    static constexpr const char* name = "{item["name"]}";',
                f"    static constexpr std::uint32_t id = 0x{item['id']:08X}U;",
                f"    static constexpr Value default_value = {_cpp_literal(item['default'], cpp)};",
            ]
        )
        if item["minimum"] is not None:
            lines.append(
                f"    static constexpr Value minimum = {_cpp_literal(item['minimum'], cpp)};"
            )
        if item["maximum"] is not None:
            lines.append(
                f"    static constexpr Value maximum = {_cpp_literal(item['maximum'], cpp)};"
            )
        lines.extend(["};", ""])
    parameter_names = ", ".join(item["cpp_name"] for item in ir["parameters"])
    lines.extend(
        [
            f"using ParameterSchema = solar::parameters::Schema<{parameter_names}>;",
            "",
            f"}} // namespace {namespace}::generated",
            "",
        ]
    )
    return "\n".join(lines)


def generate_contract_cpp(ir: dict[str, Any]) -> str:
    namespace = ir["application"]["namespace"]
    lines = _generated_header(
        "cstdint",
        "solar/core/type_list.hpp",
        "solar/generated/parameters.hpp",
        "solar/generated/types.hpp",
    )
    lines.extend([f"namespace {namespace}::generated", "{", ""])
    for item in ir["data_declarations"]:
        lines.extend(
            [
                f"struct {item['cpp_name']}",
                "{",
                f"    using Value = {item['type']['cpp']};",
                f'    static constexpr const char* name = "{item["name"]}";',
                f"    static constexpr std::uint32_t id = 0x{item['id']:08X}U;",
                f"    static constexpr bool query = {'true' if item['query'] else 'false'};",
                f"    static constexpr bool update = {'true' if item['update'] else 'false'};",
                "};",
                "",
            ]
        )
    for item in ir["actions"]:
        lines.extend(
            [
                f"struct {item['cpp_name']}",
                "{",
                f"    using Request = {item['request']['cpp']};",
                f"    using Response = {item['response']['cpp']};",
                f'    static constexpr const char* name = "{item["name"]}";',
                f"    static constexpr std::uint32_t id = 0x{item['id']:08X}U;",
                "};",
                "",
            ]
        )
    for item in ir["stream_declarations"]:
        lines.extend(
            [
                f"struct {item['cpp_name']}",
                "{",
                f"    using Value = {item['type']['cpp']};",
                f'    static constexpr const char* name = "{item["name"]}";',
                f"    static constexpr std::uint32_t id = 0x{item['id']:08X}U;",
                f"    static constexpr bool input = {'true' if item['direction'] == 'in' else 'false'};",
                f"    static constexpr std::uint32_t maximum_rate_hz = {item['maximum_rate_hz']}U;",
                "};",
                "",
            ]
        )
    action_names = ", ".join(item["cpp_name"] for item in ir["actions"])
    data_names = ", ".join(item["cpp_name"] for item in ir["data_declarations"])
    output_names = ", ".join(
        item["cpp_name"]
        for item in ir["stream_declarations"]
        if item["direction"] == "out"
    )
    input_names = ", ".join(
        item["cpp_name"]
        for item in ir["stream_declarations"]
        if item["direction"] == "in"
    )
    lines.extend(
        [
            "struct Contract",
            "{",
            "    using Parameters = typename ParameterSchema::Entries;",
            f"    using Data = solar::TypeList<{data_names}>;",
            f"    using Actions = solar::TypeList<{action_names}>;",
            f"    using OutputStreams = solar::TypeList<{output_names}>;",
            f"    using InputStreams = solar::TypeList<{input_names}>;",
            "    using Events = solar::TypeList<>;",
            "    using Metrics = solar::TypeList<>;",
            "};",
            "",
            f'inline constexpr const char* interface_sha256 = "{_fingerprint(ir["manifest"])}";',
            "",
            f"}} // namespace {namespace}::generated",
            "",
        ]
    )
    return "\n".join(lines)


def generate_app_cpp(ir: dict[str, Any]) -> str:
    return "\n".join(
        _generated_header(
            "solar/generated/types.hpp",
            "solar/generated/parameters.hpp",
            "solar/generated/contract.hpp",
        )
    )


def generate_remote_cpp(ir: dict[str, Any]) -> str:
    """Emit a typed adapter from generated declarations to Remote.

    The adapter receives the canonical parameter facade and compile-time
    endpoint dispatcher as template arguments. It owns no nullable callbacks
    and performs no runtime registration.
    """
    namespace = ir["application"]["namespace"]
    lines = [
        "// Generated by Solar codegen; do not edit.",
        "#pragma once",
        "",
        "#include <solar/remote.hpp>",
        "#include <solar/generated/app.hpp>",
        "",
    ]

    def field_expression(cpp: str, field: dict[str, Any]) -> str:
        attributes: list[str] = []
        if field.get("description"):
            attributes.append(
                f'remote::Description<"{field["description"]}">'
            )
        if field.get("unit"):
            attributes.append(f'remote::Unit<"{field["unit"]}">')
        suffix = ", " + ", ".join(attributes) if attributes else ""
        return (
            f'remote::Field<{field["id"]}, "{field["name"]}", '
            f'&{cpp}::{_identifier(field["name"])}{suffix}>'
        )

    # Parameter wire wrappers are intentionally distinct from scalar storage.
    for item in ir["parameters"]:
        lines.extend(
            [
                f"namespace {namespace}::generated {{ struct {item['wrapper']} {{ {item['type']['cpp']} value{{}}; }}; }}",
                "",
            ]
        )

    for item in ir["types"]:
        cpp = f"{namespace}::generated::{item['cpp_name']}"
        lines.extend([f"template <> struct solar::remote::Schema<{cpp}>", "{"])
        schema = next(
            schema for schema in ir["manifest"]["schemas"] if schema["id"] == item["id"]
        )
        lines.extend(
            [
                "    static constexpr SchemaDescriptor descriptor{",
                f'        .id = TypeId{{0x{item["id"]:08X}U}}, .name = "{item["name"]}", .description = "{item["description"]}", .version = {item["version"]}}};',
            ]
        )
        if item["kind"] == "enum":
            lines.extend(
                [
                    "    static constexpr SchemaShape shape = SchemaShape::Enumeration;",
                    "    static constexpr EnumOpenness openness = "
                    + (
                        "EnumOpenness::Open;"
                        if item["open"]
                        else "EnumOpenness::Closed;"
                    ),
                    "    using Values = remote::EnumValues<",
                ]
            )
            values = list(item["values"].items())
            for index, (name, _value) in enumerate(values):
                comma = "," if index + 1 < len(values) else ">;"
                lines.append(
                    f'        remote::EnumValue<{cpp}::{_identifier(name, upper=True)}, "{name}">{comma}'
                )
        else:
            field_lines = [
                field_expression(cpp, field)
                for field in sorted(item["fields"], key=lambda field: field["id"])
            ]
            lines.append(
                "    using Fields = remote::Fields<" + ", ".join(field_lines) + ">;"
            )
            lines.append(
                f"    static constexpr std::size_t max_encoded_size = {schema['max_encoded_size']};"
            )
            lines.append("    static constexpr Codec codec = Codec::Cbor;")
        lines.extend(["};", ""])

    for item in ir["parameters"]:
        wrapper = f"{namespace}::generated::{item['wrapper']}"
        lines.extend(
            [
                f"template <> struct solar::remote::Schema<{wrapper}>",
                "{",
                "    static constexpr SchemaDescriptor descriptor{",
                f'        .id = TypeId{{0x{item["schema"]:08X}U}}, .name = "{next(s["name"] for s in ir["manifest"]["schemas"] if s["id"] == item["schema"])}", .description = "{item["description"]}"}};',
                f'    using Fields = remote::Fields<{field_expression(wrapper, {"id": 1, "name": "value", "description": item["description"], "unit": item["unit"]})}>;',
                f"    static constexpr std::size_t max_encoded_size = {next(s['max_encoded_size'] for s in ir['manifest']['schemas'] if s['id'] == item['schema'])};",
                "    static constexpr Codec codec = Codec::Cbor;",
                "};",
                "",
            ]
        )

    lines.extend([f"namespace {namespace}::generated", "{", ""])

    remote_data: list[str] = []
    remote_streams: list[str] = []
    for item in ir["stream_groups"]:
        lines.extend(
            [
                f"struct {item['cpp_name']}",
                "{",
                f'    static constexpr solar::remote::InStreamGroupDescriptor descriptor{{.id = solar::remote::InStreamGroupId{{0x{item["id"]:08X}U}}, .name = "{item["name"]}", .description = "{item["description"]}", .version = {item["version"]}}};',
                "};",
                "",
            ]
        )
    for item in ir["parameters"]:
        name = item["cpp_name"] + "Remote"
        remote_data.append(f"{name}<Parameters>")
        lines.extend(
            [
                "template <typename Parameters>",
                f"struct {name}",
                "{",
                f"    using Value = {item['wrapper']};",
                f'    static constexpr solar::remote::DataDescriptor descriptor{{.id = solar::remote::DataId{{0x{item["id"]:08X}U}}, .name = "{item["name"]}", .description = "{item["description"]}", .version = {item["version"]}}};',
                f"    static Value read() noexcept {{ const auto value = Parameters::template get<{item['cpp_name']}>(); return {{.value = value ? *value : {item['cpp_name']}::default_value}}; }}",
                f"    static solar::Result<void> write(const Value& value) noexcept {{ auto result = Parameters::template set<{item['cpp_name']}>(value.value); return result ? solar::Result<void>{{}} : solar::fail<solar::Error>(result.error()); }}",
                "    using Execution = solar::remote::Inline;",
                "    using Capabilities = solar::remote::Capabilities<solar::remote::Query<&read>, solar::remote::Update<&write>>;",
                "};",
                "",
            ]
        )
    for item in ir["data_declarations"]:
        name = item["cpp_name"] + "Remote"
        remote_data.append(f"{name}<Endpoints>")
        data_capabilities = []
        lines.extend(
            [
                "template <typename Endpoints>",
                f"struct {name}",
                "{",
                f"    using Value = {item['type']['cpp']};",
                f'    static constexpr solar::remote::DataDescriptor descriptor{{.id = solar::remote::DataId{{0x{item["id"]:08X}U}}, .name = "{item["name"]}", .description = "{item["description"]}", .version = {item["version"]}}};',
                "    using Execution = solar::remote::Inline;",
            ]
        )
        if item["query"]:
            lines.append(
                f"    static Value read() noexcept {{ return Endpoints::template query<{item['cpp_name']}>(); }}"
            )
            data_capabilities.append("solar::remote::Query<&read>")
        if item["update"]:
            lines.append(
                f"    static solar::Result<void> write(const Value& value) noexcept {{ return Endpoints::template update<{item['cpp_name']}>(value); }}"
            )
            data_capabilities.append("solar::remote::Update<&write>")
        lines.extend(
            [
                "    using Capabilities = solar::remote::Capabilities<"
                + ", ".join(data_capabilities)
                + ">;",
                "};",
                "",
            ]
        )
    for item in ir["stream_declarations"]:
        name = item["cpp_name"] + "Remote"
        remote_streams.append(name if item["direction"] == "out" else f"{name}<Endpoints>")
        lines.extend(
            [
                *(["template <typename Endpoints>"] if item["direction"] == "in" else []),
                f"struct {name}",
                "{",
                f"    using Value = {item['type']['cpp']};",
                f'    static constexpr solar::remote::StreamDescriptor descriptor{{.id = solar::remote::StreamId{{0x{item["id"]:08X}U}}, .name = "{item["name"]}", .description = "{item["description"]}", .version = {item["version"]}}};',
                f"    static constexpr bool input = {'true' if item['direction'] == 'in' else 'false'};",
            ]
        )
        if item["direction"] == "out":
            lines.extend(
                [
                    f"    static constexpr std::uint32_t maximum_rate_hz = {item['maximum_rate_hz']}U;",
                    "    using Capabilities = solar::remote::Capabilities<>;",
                ]
            )
        else:
            policies = [
                "solar::remote::OnOpen<&opened>",
                "solar::remote::OnClose<&closed>",
            ]
            group = item["exclusive_group"]
            if group is not None:
                behavior = (
                    "solar::remote::Replace"
                    if group["replacement"] == "replace"
                    else "solar::remote::RejectExisting"
                )
                policies.append(
                    f"solar::remote::Exclusive<{group['cpp_name']}, {behavior}>"
                )
            policies.extend(
                [
                    "solar::remote::ReliableWindow<4>",
                    f"solar::remote::MaxRate<{item['maximum_rate_hz']}>",
                    "solar::remote::Inline",
                ]
            )
            lines.extend(
                [
                    f"    static solar::Result<void> consume(const Value& value) noexcept {{ return Endpoints::template consume<{item['cpp_name']}>(value); }}",
                    f"    static solar::Result<void> opened(const solar::remote::InStreamOpenContext& context) noexcept {{ return Endpoints::template open<{item['cpp_name']}>(context); }}",
                    f"    static void closed(const solar::remote::InStreamCloseContext& context) noexcept {{ Endpoints::template close<{item['cpp_name']}>(context); }}",
                    "    using Capabilities = solar::remote::Capabilities<solar::remote::InStream<&consume, "
                    + ", ".join(policies)
                    + ">>;",
                ]
            )
        lines.extend(["};", ""])

    remote_actions: list[str] = []
    for item in ir["actions"]:
        name = item["cpp_name"] + "Remote"
        remote_actions.append(f"{name}<Endpoints>")
        lines.extend(
            [
                "template <typename Endpoints>",
                f"struct {name}",
                "{",
                f"    using Request = {item['request']['cpp']};",
                f"    using Response = {item['response']['cpp']};",
                f'    static constexpr solar::remote::ActionDescriptor descriptor{{.id = solar::remote::ActionId{{0x{item["id"]:08X}U}}, .name = "{item["name"]}", .description = "{item["description"]}", .version = {item["version"]}}};',
                "    using Execution = solar::remote::Inline;",
                f"    static Response execute(const Request& request) noexcept {{ return Endpoints::template call<{item['cpp_name']}>(request); }}",
                "};",
                "",
            ]
        )
    lines.extend(
        [
            "template <typename Parameters, typename Endpoints> struct RemoteContract",
            "{",
            "using RemoteSchemas = solar::remote::ContributeSchemas<"
            + ", ".join(
                item["cpp_name"] for item in ir["types"] if item["kind"] == "enum"
            )
            + ">;",
            "using RemoteData = solar::remote::ContributeData<"
            + ", ".join(remote_data)
            + ">;",
            "using RemoteActions = solar::remote::ContributeActions<"
            + ", ".join(remote_actions)
            + ">;",
            "using RemoteStreams = solar::remote::ContributeStreams<"
            + ", ".join(remote_streams)
            + ">;",
            "};",
            "",
            "template <typename ParameterModule, typename EndpointDispatch>",
            "struct ServerContract",
            "{",
            "    using Parameters = ParameterModule;",
            "    using Endpoints = EndpointDispatch;",
            "    using Declarations = Contract;",
            "};",
            "",
            f"}} // namespace {namespace}::generated",
            "",
        ]
    )
    return "\n".join(lines)


def _python_annotation(
    type_name: str, types: dict[str, dict[str, Any]], *, qualified: bool = False
) -> str:
    primitive = {
        "bool": "bool",
        "u8": "int",
        "u16": "int",
        "u32": "int",
        "u64": "int",
        "i8": "int",
        "i16": "int",
        "i32": "int",
        "i64": "int",
        "f32": "float",
        "f64": "float",
    }.get(type_name)
    if primitive is not None:
        return primitive
    bounded = re.fullmatch(r"(string|bytes)<[1-9][0-9]*>", type_name)
    if bounded:
        return "str" if bounded.group(1) == "string" else "bytes"
    optional = re.fullmatch(r"optional<(.+)>", type_name)
    if optional:
        return f"{_python_annotation(optional.group(1).strip(), types, qualified=qualified)} | None"
    collection = re.fullmatch(r"(array|sequence)<(.+),\s*[1-9][0-9]*>", type_name)
    if collection:
        value = _python_annotation(collection.group(2).strip(), types, qualified=qualified)
        return f"tuple[{value}, ...]"
    name = types[type_name]["cpp_name"]
    return f"models.{name}" if qualified else name


def _python_validation(type_name: str, expression: str, indent: str) -> list[str]:
    integer_bounds = {
        "u8": (0, 2**8 - 1), "u16": (0, 2**16 - 1),
        "u32": (0, 2**32 - 1), "u64": (0, 2**64 - 1),
        "i8": (-(2**7), 2**7 - 1), "i16": (-(2**15), 2**15 - 1),
        "i32": (-(2**31), 2**31 - 1), "i64": (-(2**63), 2**63 - 1),
    }
    if type_name in integer_bounds:
        minimum, maximum = integer_bounds[type_name]
        return [f"{indent}if not {minimum} <= {expression} <= {maximum}:",
                f'{indent}    raise ValueError("{expression} is outside {type_name}")']
    bounded = re.fullmatch(r"(string|bytes)<([1-9][0-9]*)>", type_name)
    if bounded:
        return [f"{indent}if len({expression}) > {bounded.group(2)}:",
                f'{indent}    raise ValueError("{expression} exceeds its declared bound")']
    optional = re.fullmatch(r"optional<(.+)>", type_name)
    if optional:
        nested = _python_validation(optional.group(1).strip(), expression, indent + "    ")
        return ([f"{indent}if {expression} is not None:"] + nested) if nested else []
    collection = re.fullmatch(r"(array|sequence)<(.+),\s*([1-9][0-9]*)>", type_name)
    if collection:
        comparison = "!=" if collection.group(1) == "array" else ">"
        lines = [f"{indent}if len({expression}) {comparison} {collection.group(3)}:",
                 f'{indent}    raise ValueError("{expression} violates its declared bound")']
        nested = _python_validation(collection.group(2).strip(), "item", indent + "    ")
        if nested:
            lines.append(f"{indent}for item in {expression}:")
            lines.extend(nested)
        return lines
    return []


def generate_python_models(ir: dict[str, Any]) -> str:
    lines = [
        '"""Generated Solar application models; do not edit."""',
        "",
        "from __future__ import annotations",
        "",
        "from dataclasses import dataclass",
        "from enum import IntEnum",
        "",
    ]
    by_source_name = {
        name: item
        for name, item in zip(
            [item["name"].rsplit(".", 1)[-1] for item in ir["types"]], ir["types"]
        )
    }
    for item in ir["types"]:
        if item["kind"] == "enum":
            base = "IntEnum" if not item["open"] else "solar_remote.OpenIntEnum"
            if item["open"] and "import solar_remote" not in lines:
                lines.insert(7, "import solar_remote")
            suffix = "  # type: ignore[misc]" if item["open"] else ""
            lines.append(f"class {item['cpp_name']}({base}):{suffix}")
            for name, value in item["values"].items():
                lines.append(f"    {_identifier(name, upper=True)} = {value}")
            lines.extend([f"    __solar_schema_id__ = 0x{item['id']:08X}", ""])
            continue
        lines.extend(
            ["@dataclass(frozen=True, slots=True)", f"class {item['cpp_name']}:"]
        )
        if not item["fields"]:
            lines.append("    pass")
        for field in item["fields"]:
            annotation = _python_annotation(field["type"], by_source_name)
            lines.append(f"    {_identifier(field['name'])}: {annotation}")
        validations = [
            line
            for field in item["fields"]
            for line in _python_validation(
                field["type"], f"self.{_identifier(field['name'])}", "        "
            )
        ]
        if validations:
            lines.extend(["", "    def __post_init__(self) -> None:", *validations])
        lines.extend(
            [
                f"    __solar_schema_id__ = 0x{item['id']:08X}",
                "",
            ]
        )
    for parameter in ir["parameters"]:
        annotation = _python_annotation(parameter["type"]["name"], by_source_name)
        lines.extend(
            [
                "@dataclass(frozen=True, slots=True)",
                f"class {parameter['wrapper']}:",
                f"    value: {annotation}",
                "",
                "    def __post_init__(self) -> None:",
                *(
                    _python_validation(parameter["type"]["name"], "self.value", "        ")
                    + ([f"        if self.value < {parameter['minimum']!r}:",
                        '            raise ValueError("value is below its declared minimum")']
                       if parameter["minimum"] is not None else [])
                    + ([f"        if self.value > {parameter['maximum']!r}:",
                        '            raise ValueError("value exceeds its declared maximum")']
                       if parameter["maximum"] is not None else [])
                    or ["        pass"]
                ),
                f"    __solar_schema_id__ = 0x{parameter['schema']:08X}",
                "",
            ]
        )
    return "\n".join(lines)


def _namespace_classes(
    category: str,
    leaves: list[tuple[str, str, str]],
) -> tuple[list[str], str]:
    tree: dict[str, Any] = {}
    for path, annotation, expression in leaves:
        node = tree
        parts = [_identifier(part) for part in path.split(".")]
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = (annotation, expression)

    lines: list[str] = []

    def emit(node: dict[str, Any], path: tuple[str, ...]) -> str:
        class_name = "_" + _pascal(category + "." + ".".join(path))
        children: list[tuple[str, str]] = []
        for name, value in node.items():
            if isinstance(value, dict):
                children.append((name, emit(value, path + (name,))))
        lines.extend([f"class {class_name}:", "    def __init__(self, session: Any):"])
        if not node:
            lines.append("        pass")
        for name, child_class in children:
            lines.append(f"        self.{name}: {child_class} = {child_class}(session)")
        for name, value in node.items():
            if isinstance(value, dict):
                continue
            annotation, expression = value
            lines.append(f"        self.{name}: {annotation} = {expression}")
        lines.append("")
        return class_name

    root = emit(tree, ())
    return lines, root


def generate_python_client(ir: dict[str, Any], digest: str, build_id: int) -> str:
    types = {item["name"].rsplit(".", 1)[-1]: item for item in ir["types"]}
    data_leaves = []
    for item in ir["data_declarations"]:
        model = f"models.{item['type']['cpp']}"
        data_leaves.append(
            (
                item["name"],
                f"solar_remote.GeneratedData[{model}]",
                f"solar_remote.GeneratedData(session, {item['name']!r}, {model})",
            )
        )
    parameter_leaves = []
    for item in ir["parameters"]:
        annotation = _python_annotation(item["type"]["name"], types, qualified=True)
        parameter_leaves.append(
            (
                item["name"],
                f"solar_remote.GeneratedParameter[{annotation}]",
                f"solar_remote.GeneratedParameter(session, {item['name']!r}, models.{item['wrapper']})",
            )
        )
    action_leaves = []
    for item in ir["actions"]:
        request = f"models.{item['request']['cpp']}"
        response = f"models.{item['response']['cpp']}"
        action_leaves.append(
            (
                item["name"],
                f"solar_remote.GeneratedAction[{request}, {response}]",
                f"solar_remote.GeneratedAction(session, {item['name']!r}, {request}, {response})",
            )
        )
    stream_leaves = []
    for item in ir["stream_declarations"]:
        model = f"models.{item['type']['cpp']}"
        runtime = (
            "GeneratedInputStream"
            if item["direction"] == "in"
            else "GeneratedOutputStream"
        )
        stream_leaves.append(
            (
                item["name"],
                f"solar_remote.{runtime}[{model}]",
                f"solar_remote.{runtime}(session, {item['name']!r}, {model})",
            )
        )

    namespace_lines: list[str] = []
    data_lines, data_root = _namespace_classes("data", data_leaves)
    parameter_lines, parameter_root = _namespace_classes("parameters", parameter_leaves)
    action_lines, action_root = _namespace_classes("actions", action_leaves)
    stream_lines, stream_root = _namespace_classes("streams", stream_leaves)
    namespace_lines.extend(data_lines)
    namespace_lines.extend(parameter_lines)
    namespace_lines.extend(action_lines)
    namespace_lines.extend(stream_lines)

    requirements = {
        "protocol": ir["manifest"]["protocol"],
        "schemas": ir["manifest"]["schemas"],
        "data": ir["manifest"]["data"],
        "actions": ir["manifest"]["actions"],
        "streams": ir["manifest"]["streams"],
        "capabilities": ir["manifest"]["capabilities"],
    }
    lines = [
        '"""Generated typed Solar application client; do not edit."""',
        "",
        "from __future__ import annotations",
        "",
        "from typing import Any",
        "",
        "import solar_remote",
        "",
        "from . import models",
        "",
        f"INTERFACE_SHA256 = bytes.fromhex({digest!r})",
        f"BUILD_ID = 0x{build_id:016X}",
        "REQUIREMENTS = " + repr(requirements),
        "",
        *namespace_lines,
        "class Robot:",
        "    interface_sha256 = INTERFACE_SHA256",
        "    build_id = BUILD_ID",
        "",
        "    def __init__(self, session: Any):",
        "        self.session = session",
        f"        self.data: {data_root} = {data_root}(session)",
        f"        self.parameters: {parameter_root} = {parameter_root}(session)",
        f"        self.actions: {action_root} = {action_root}(session)",
        f"        self.streams: {stream_root} = {stream_root}(session)",
        "        self.dynamic = session.robot() if hasattr(session, 'robot') else None",
        "",
        "    @classmethod",
        "    async def bind(",
        "        cls,",
        "        session: Any,",
        "        *,",
        "        interface_policy: solar_remote.InterfacePolicy = solar_remote.InterfacePolicy.EXACT,",
        "        build_policy: solar_remote.BuildPolicy = solar_remote.BuildPolicy.ANY,",
        "    ) -> 'Robot':",
        "        solar_remote.validate_generated_interface(",
        "            session,",
        "            expected_digest=INTERFACE_SHA256,",
        "            requirements=REQUIREMENTS,",
        "            interface_policy=interface_policy,",
        "            expected_build=BUILD_ID,",
        "            build_policy=build_policy,",
        "        )",
        "        return cls(session)",
        "",
    ]
    return "\n".join(lines)


def generate_python_package(
    ir: dict[str, Any], output: Path, digest: str, build_id: int
) -> None:
    distribution = ir["application"]["name"].replace("_", "-") + "-solar-client"
    package = ir["application"]["python_package"]
    root = output / "python"
    package_root = root / package
    package_root.mkdir(parents=True, exist_ok=True)
    (package_root / "models.py").write_text(generate_python_models(ir))
    (package_root / "client.py").write_text(
        generate_python_client(ir, digest, build_id)
    )
    (package_root / "__init__.py").write_text(
        '"""Generated Solar application package."""\n\n'
        "from .client import BUILD_ID, INTERFACE_SHA256, Robot\n"
        "from . import models\n\n"
        '__all__ = ["BUILD_ID", "INTERFACE_SHA256", "Robot", "models"]\n'
    )
    (package_root / "py.typed").write_text("")
    (root / "pyproject.toml").write_text(
        "[build-system]\n"
        'requires = ["hatchling>=1.26"]\n'
        'build-backend = "hatchling.build"\n\n'
        "[project]\n"
        f'name = "{distribution}"\n'
        'version = "0.1.0"\n'
        f'description = "Generated Solar client for {ir["application"]["name"]}"\n'
        'requires-python = ">=3.11"\n'
        'dependencies = ["solar-remote>=0.1,<0.2"]\n\n'
        "[tool.hatch.build.targets.wheel]\n"
        f'packages = ["{package}"]\n'
    )


def write_outputs(
    project: Path, output: Path, lock_path: Path, *, update_lock: bool
) -> None:
    ir, lock, compatibility = compile_project(project, lock_path)
    output.mkdir(parents=True, exist_ok=True)
    cpp_output = output / "solar" / "generated"
    cpp_output.mkdir(parents=True, exist_ok=True)
    canonical_ir = json.dumps(ir, indent=2, sort_keys=True) + "\n"
    (output / "interface.ir.json").write_text(canonical_ir)
    (output / "manifest.json").write_text(
        json.dumps(ir["manifest"], indent=2, sort_keys=True) + "\n"
    )
    interface_image = json.dumps(
        ir["manifest"], sort_keys=True, separators=(",", ":")
    ).encode()
    interface_digest = hashlib.sha256(interface_image).hexdigest()
    (output / "manifest.bin").write_bytes(interface_image)
    (output / "manifest.sha256").write_text(interface_digest + "\n")
    (output / "compatibility.json").write_text(
        json.dumps(compatibility, indent=2, sort_keys=True) + "\n"
    )
    (cpp_output / "types.hpp").write_text(generate_types_cpp(ir))
    (cpp_output / "parameters.hpp").write_text(generate_parameters_cpp(ir))
    (cpp_output / "contract.hpp").write_text(generate_contract_cpp(ir))
    (cpp_output / "app.hpp").write_text(generate_app_cpp(ir))
    (cpp_output / "remote.hpp").write_text(generate_remote_cpp(ir))
    build_id = int.from_bytes(
        hashlib.sha256(canonical_ir.encode()).digest()[:8], "little"
    )
    generate_python_package(ir, output, interface_digest, build_id)
    (output / "interface.d").write_text(
        f"{cpp_output / 'app.hpp'}: "
        + " ".join(str((project.parent / item).resolve()) for item in ir["dependencies"])
        + "\n"
    )
    if update_lock or not lock_path.exists():
        lock_path.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", action="version", version=f"solar-codegen {GENERATOR_VERSION}")
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--update-lock", action="store_true")
    args = parser.parse_args(argv)
    try:
        write_outputs(
            args.project.resolve(),
            args.output.absolute(),
            args.lock.absolute(),
            update_lock=args.update_lock,
        )
    except (CompileError, OSError, json.JSONDecodeError) as error:
        print(f"solar generation failed: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
