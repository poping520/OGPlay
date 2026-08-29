#!/usr/bin/env python3
"""Validate OGPlay GLES IDL and generate a deterministic C++ catalog."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import tempfile
from typing import Any, Sequence


SCALAR_TYPES = {
    "GLbitfield", "GLboolean", "GLbyte", "GLclampf", "GLenum", "GLfloat",
    "GLchar", "GLint", "GLintptr", "GLshort", "GLsizei", "GLsizeiptr", "GLubyte",
    "GLclampx", "GLfixed", "GLuint", "GLushort", "void",
}
RETURN_TYPES = SCALAR_TYPES | {"const GLubyte*", "void*"}
DIRECTIONS = {"in", "out", "inout"}


class IdlError(ValueError):
    pass


def _require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise IdlError(f"{field} must be a non-empty string")
    return value


def validate_idl(document: Any) -> dict[str, Any]:
    if not isinstance(document, dict):
        raise IdlError("IDL root must be an object")
    if document.get("schema_version") != 1:
        raise IdlError("schema_version must be 1")
    if document.get("api") not in {"gles1", "gles1_extensions", "gles2"}:
        raise IdlError("api must be gles1, gles1_extensions or gles2")
    if document.get("header_scope", "complete") not in {"complete", "subset"}:
        raise IdlError("header_scope must be complete or subset")
    _require_string(document.get("library"), "library")
    functions = document.get("functions")
    if not isinstance(functions, list) or not functions:
        raise IdlError("functions must be a non-empty array")
    names: list[str] = []
    thunk_ids: list[int | None] = []
    for function_index, function in enumerate(functions):
        prefix = f"functions[{function_index}]"
        if not isinstance(function, dict):
            raise IdlError(f"{prefix} must be an object")
        name = _require_string(function.get("name"), f"{prefix}.name")
        if not name.startswith("gl"):
            raise IdlError(f"{prefix}.name must start with gl")
        names.append(name)
        thunk_id = function.get("thunk_id")
        if thunk_id is not None and (not isinstance(thunk_id, int) or
                                     isinstance(thunk_id, bool) or
                                     thunk_id < 0):
            raise IdlError(f"{prefix}.thunk_id must be a non-negative integer")
        thunk_ids.append(thunk_id)
        return_type = _require_string(function.get("return"), f"{prefix}.return")
        if return_type not in RETURN_TYPES:
            raise IdlError(f"{prefix}.return has unknown type {return_type}")
        parameters = function.get("parameters")
        if not isinstance(parameters, list):
            raise IdlError(f"{prefix}.parameters must be an array")
        parameter_names: set[str] = set()
        for parameter_index, parameter in enumerate(parameters):
            item = f"{prefix}.parameters[{parameter_index}]"
            if not isinstance(parameter, dict):
                raise IdlError(f"{item} must be an object")
            parameter_name = _require_string(parameter.get("name"), f"{item}.name")
            if parameter_name in parameter_names:
                raise IdlError(f"{prefix} has duplicate parameter {parameter_name}")
            parameter_names.add(parameter_name)
            value_type = _require_string(parameter.get("type"), f"{item}.type")
            if value_type not in SCALAR_TYPES or value_type == "void" and not parameter.get("pointer"):
                raise IdlError(f"{item} has invalid type {value_type}")
            pointer = parameter.get("pointer", False)
            if not isinstance(pointer, bool):
                raise IdlError(f"{item}.pointer must be boolean")
            transfer_fields = {"direction", "nullable", "count"}
            if pointer:
                indirection = parameter.get("indirection", 1)
                if not isinstance(indirection, int) or indirection not in {1, 2}:
                    raise IdlError(f"{item}.indirection must be 1 or 2")
                if parameter.get("direction") not in DIRECTIONS:
                    raise IdlError(f"{item}.direction is required for pointers")
                if not isinstance(parameter.get("nullable"), bool):
                    raise IdlError(f"{item}.nullable is required for pointers")
                _require_string(parameter.get("count"), f"{item}.count")
            elif transfer_fields.intersection(parameter):
                raise IdlError(f"{item} scalar must not declare transfer metadata")
            elif "indirection" in parameter:
                raise IdlError(f"{item} scalar must not declare indirection")
    if names != sorted(names) or len(names) != len(set(names)):
        raise IdlError("function names must be unique and sorted")
    if any(thunk_id is not None for thunk_id in thunk_ids):
        if any(thunk_id is None for thunk_id in thunk_ids):
            raise IdlError("thunk_id must be present on every function or none")
        if sorted(thunk_ids) != list(range(len(functions))):
            raise IdlError("thunk_id values must be unique and contiguous from zero")
    return document


def load_idl(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise IdlError(f"cannot read {path}: {error}") from error
    return validate_idl(document)


def _cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def generate_header(document: dict[str, Any]) -> str:
    api = document["api"]
    parameters: list[dict[str, Any]] = []
    function_rows: list[str] = []
    functions = (sorted(document["functions"],
                        key=lambda function: function["thunk_id"])
                 if "thunk_id" in document["functions"][0]
                 else document["functions"])
    for function in functions:
        offset = len(parameters)
        parameters.extend(function["parameters"])
        function_rows.append(
            "    {" + ", ".join((
                _cpp_string(function["name"]),
                _cpp_string(function["return"]),
                str(offset) + "U",
                str(len(function["parameters"])) + "U",
            )) + "},"
        )
    parameter_rows: list[str] = []
    for parameter in parameters:
        pointer = parameter.get("pointer", False)
        parameter_rows.append(
            "    {" + ", ".join((
                _cpp_string(parameter["name"]),
                _cpp_string(parameter["type"]),
                _cpp_string(parameter.get("direction", "value")),
                _cpp_string(parameter.get("count", "")),
                str(parameter.get("indirection", 1)) + "U" if pointer else "0U",
                "true" if parameter.get("nullable", False) else "false",
            )) + "},"
        )
    lines = [
        "// Generated by tools/generate_gles_api.py. Do not edit.",
        "#pragma once", "", "#include <array>", "#include <cstddef>",
        "#include <string_view>", "",
        f"namespace ogplay::gles::generated::{api} {{", "",
        "struct ParameterSpec final {",
        "    std::string_view name;", "    std::string_view type;",
        "    std::string_view direction;", "    std::string_view count;",
        "    std::size_t indirection{};", "    bool nullable{};", "};", "",
        "struct FunctionSpec final {", "    std::string_view name;",
        "    std::string_view return_type;", "    std::size_t parameter_offset{};",
        "    std::size_t parameter_count{};", "};", "",
        f"inline constexpr std::string_view kApi = {_cpp_string(api)};",
        f"inline constexpr std::string_view kLibrary = {_cpp_string(document['library'])};",
        f"inline constexpr std::array<ParameterSpec, {len(parameters)}> kParameters{{{{",
        *parameter_rows, "}};", "",
        f"inline constexpr std::array<FunctionSpec, {len(function_rows)}> kFunctions{{{{",
        *function_rows, "}};", "",
        f"}}  // namespace ogplay::gles::generated::{api}", "",
    ]
    return "\n".join(lines)


def verify_header(document: dict[str, Any], header_paths: Sequence[Path]) -> None:
    header = ""
    for header_path in header_paths:
        try:
            header += header_path.read_text(encoding="utf-8")
        except OSError as error:
            raise IdlError(f"cannot read GLES header {header_path}: {error}") from error
    declared = set(re.findall(
        r"(?:GL_APICALL|GL_API)\s+.+?\s*GL_APIENTRY\s+"
        r"(gl[A-Za-z0-9_]+)\s*\(", header
    ))
    catalog = {function["name"] for function in document["functions"]}
    missing = ([] if document.get("header_scope", "complete") == "subset"
               else sorted(declared - catalog))
    extra = sorted(catalog - declared)
    if missing or extra:
        raise IdlError(
            f"IDL/header function mismatch: missing={missing}, extra={extra}"
        )


def write_or_check(idl_path: Path, output: Path, check: bool,
                   header_paths: Sequence[Path] | None = None) -> int:
    document = load_idl(idl_path)
    if header_paths:
        verify_header(document, header_paths)
    expected = generate_header(document)
    if check:
        if not output.is_file() or output.read_text(encoding="utf-8") != expected:
            raise IdlError(f"generated output is stale: {output}")
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(expected)
    return 0


def self_test() -> int:
    valid = {"schema_version": 1, "api": "gles2", "library": "libGLESv2.so",
             "functions": [{"name": "glClear", "return": "void",
                             "parameters": [{"name": "mask", "type": "GLbitfield"}]}]}
    validate_idl(valid)
    header = generate_header(valid)
    assert 'kApi = "gles2"' in header and '"glClear", "void", 0U, 1U' in header
    invalid = json.loads(json.dumps(valid))
    invalid["functions"][0]["parameters"][0]["pointer"] = True
    try:
        validate_idl(invalid)
        raise AssertionError("pointer without transfer metadata was accepted")
    except IdlError:
        pass
    stable = json.loads(json.dumps(valid))
    stable["functions"] = [
        {"name": "glAlpha", "return": "void", "parameters": [],
         "thunk_id": 1},
        {"name": "glClear", "return": "void", "parameters": [],
         "thunk_id": 0},
    ]
    validate_idl(stable)
    stable_header = generate_header(stable)
    assert stable_header.index('"glClear"') < stable_header.index('"glAlpha"')
    incomplete_stable = json.loads(json.dumps(stable))
    del incomplete_stable["functions"][0]["thunk_id"]
    try:
        validate_idl(incomplete_stable)
        raise AssertionError("partial thunk_id catalog was accepted")
    except IdlError:
        pass
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source, output = root / "api.json", root / "api.h"
        source.write_text(json.dumps(valid), encoding="utf-8")
        write_or_check(source, output, False)
        write_or_check(source, output, True)
        header_path = root / "gl.h"
        header_path.write_text(
            "GL_APICALL void GL_APIENTRY glClear(GLbitfield mask);\n"
            "GL_APICALL void GL_APIENTRY glExtra(void);\n", encoding="utf-8")
        subset = json.loads(json.dumps(valid))
        subset["header_scope"] = "subset"
        source.write_text(json.dumps(subset), encoding="utf-8")
        write_or_check(source, output, False, [header_path])
    print("GLES IDL generator self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--idl", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--verify-header", type=Path, action="append")
    args = parser.parse_args(argv)
    if not args.self_test and (args.idl is None or args.output is None):
        parser.error("--idl and --output are required")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        return self_test() if args.self_test else write_or_check(
            args.idl, args.output, args.check, args.verify_header)
    except IdlError as error:
        print(f"GLES IDL error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
