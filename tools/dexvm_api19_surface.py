#!/usr/bin/env python3
"""Extract a deterministic Java class-shape inventory from pinned API-19 sources."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

SCHEMA = 1
PRIMITIVES = {
    "void": "V", "boolean": "Z", "byte": "B", "char": "C",
    "short": "S", "int": "I", "long": "J", "float": "F", "double": "D",
}
MODIFIERS = frozenset({
    "public", "protected", "private", "static", "final", "abstract",
    "synchronized", "native", "strictfp", "transient", "volatile",
})


def erase_generics(value: str) -> str:
    output: list[str] = []
    depth = 0
    for char in value:
        if char == "<":
            depth += 1
        elif char == ">":
            depth -= 1
        elif depth == 0:
            output.append(char)
    return "".join(output)


def split_top_level(value: str, separator: str = ",") -> list[str]:
    parts: list[str] = []
    start = 0
    depth = 0
    for index, char in enumerate(value):
        if char in "<([":
            depth += 1
        elif char in ">)]":
            depth -= 1
        elif char == separator and depth == 0:
            parts.append(value[start:index].strip())
            start = index + 1
    tail = value[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def strip_source(text: str) -> str:
    output: list[str] = []
    index = 0
    quote = ""
    escaped = False
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if quote:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            index += 1
        elif char in "\"'":
            quote = char
            output.append(char)
            index += 1
        elif char == "/" and following == "/":
            index += 2
            while index < len(text) and text[index] != "\n":
                index += 1
            output.append("\n")
            index += 1
        elif char == "/" and following == "*":
            index += 2
            while index + 1 < len(text) and text[index:index + 2] != "*/":
                output.append("\n" if text[index] == "\n" else " ")
                index += 1
            index += 2
        else:
            output.append(char)
            index += 1
    clean = "".join(output)
    return re.sub(r"@(?!interface\b)\w+(?:\.\w+)*(?:\s*\([^)]*\))?", " ", clean)


def matching_brace(text: str, opening: int) -> int:
    depth = 0
    quote = ""
    escaped = False
    for index in range(opening, len(text)):
        char = text[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unbalanced Java class body")


def java_descriptor(type_name: str, package: str, imports: dict[str, str],
                    wildcard_imports: list[str], type_variables: set[str],
                    available_types: set[str]) -> str:
    value = erase_generics(type_name).strip()
    dimensions = value.count("[]") + (1 if value.endswith("...") else 0)
    value = value.replace("[]", "").removesuffix("...").strip()
    if value.startswith("?") or value in type_variables:
        value = "java.lang.Object"
    base = PRIMITIVES.get(value)
    if base is None:
        first, dot, remainder = value.partition(".")
        if first in imports:
            qualified = imports[first] + (("$" + remainder.replace(".", "$")) if dot else "")
        elif dot and first[:1].islower():
            qualified = value
        else:
            candidates = ["java.lang." + value, package + "." + value]
            candidates.extend(prefix + "." + value for prefix in wildcard_imports)
            qualified = next((candidate for candidate in candidates
                              if candidate in available_types), candidates[1])
        base = "L" + qualified.replace(".", "/") + ";"
    return "[" * dimensions + base


def modifiers_and_rest(declaration: str) -> tuple[list[str], str]:
    words = declaration.strip().split()
    modifiers: list[str] = []
    while words and words[0] in MODIFIERS:
        modifiers.append(words.pop(0))
    return modifiers, " ".join(words)


def member_declarations(body: str) -> list[str]:
    declarations: list[str] = []
    start = 0
    index = 0
    while index < len(body):
        char = body[index]
        if char == ";":
            declarations.append(body[start:index].strip())
            start = index + 1
        elif char == "{":
            prefix = body[start:index].strip()
            if "(" in prefix and ")" in prefix:
                declarations.append(prefix)
            index = matching_brace(body, index)
            start = index + 1
        index += 1
    return [re.sub(r"\s+", " ", item).strip() for item in declarations if item]


def parse_java(text: str, source_name: str,
               available_types: set[str] | None = None) -> dict:
    clean = strip_source(text)
    package_match = re.search(r"\bpackage\s+([\w.]+)\s*;", clean)
    if not package_match:
        raise ValueError(f"{source_name}: package declaration missing")
    package = package_match.group(1)
    imports = {
        value.rsplit(".", 1)[-1]: value
        for value in re.findall(r"\bimport\s+([\w.]+)\s*;", clean)
        if not value.endswith(".*")
    }
    wildcard_imports = re.findall(r"\bimport\s+([\w.]+)\.\*\s*;", clean)
    available = available_types or set(imports.values()) | {
        "java.lang.Object", "java.lang.String", "java.lang.Class",
        "java.lang.Throwable", "java.lang.Exception", "java.lang.RuntimeException",
        "java.lang.CharSequence", "java.lang.Comparable", "java.lang.Iterable",
        "java.lang.Runnable",
    }
    simple_name = Path(source_name).stem
    header_pattern = re.compile(
        rf"\b(public\s+)?(?:(?:abstract|final|strictfp)\s+)*"
        rf"(class|interface|@interface|enum)\s+{re.escape(simple_name)}\b([^{{]*){{")
    header = header_pattern.search(clean)
    if not header:
        raise ValueError(f"{source_name}: top-level type {simple_name} missing")
    kind = "interface" if header.group(2) == "@interface" else header.group(2)
    tail = erase_generics(header.group(3))
    opening = header.end() - 1
    body = clean[opening + 1:matching_brace(clean, opening)]
    type_variables = set(re.findall(r"\b([A-Z])\b", header.group(3)))

    superclass = None
    extends = re.search(r"\bextends\s+([\w.$]+)", tail)
    if kind == "class" and extends:
        superclass = java_descriptor(extends.group(1), package, imports,
                                     wildcard_imports, type_variables, available)
    elif kind == "class" and simple_name != "Object":
        superclass = "Ljava/lang/Object;"
    interfaces: list[str] = []
    relation = "extends" if kind == "interface" else "implements"
    related = re.search(rf"\b{relation}\s+(.+)$", tail)
    if related:
        interfaces = [java_descriptor(item, package, imports, wildcard_imports,
                                      type_variables, available)
                      for item in split_top_level(related.group(1))]

    methods: list[dict] = []
    fields: list[dict] = []
    for declaration in member_declarations(body):
        modifiers, rest = modifiers_and_rest(declaration)
        if not ({"public", "protected"} & set(modifiers)):
            continue
        method_type_variables = set(re.findall(r"\b([A-Z])(?:\s+extends\b|\s*[,>])",
                                               rest.split("(", 1)[0]))
        rest = re.sub(r"^<[^>]+>\s*", "", rest)
        member_type_variables = type_variables | method_type_variables
        if "(" in rest and ("=" not in rest or rest.index("(") < rest.index("=")):
            match = re.match(r"(.+?)\s+(\w+)\s*\((.*)\)(?:\s+throws\s+.+)?$", rest)
            constructor = re.match(rf"{re.escape(simple_name)}\s*\((.*)\)(?:\s+throws\s+.+)?$", rest)
            if constructor:
                name = "<init>"
                result = "V"
                parameters = constructor.group(1)
            elif match:
                result = java_descriptor(match.group(1), package, imports,
                                         wildcard_imports, member_type_variables,
                                         available)
                name = match.group(2)
                parameters = match.group(3)
            else:
                continue
            arguments: list[str] = []
            for parameter in split_top_level(parameters):
                parameter = re.sub(r"\bfinal\s+", "", parameter).strip()
                pieces = parameter.rsplit(" ", 1)
                if len(pieces) != 2:
                    raise ValueError(f"{source_name}: cannot parse parameter {parameter}")
                arguments.append(java_descriptor(
                    pieces[0], package, imports, wildcard_imports,
                    member_type_variables, available))
            methods.append({"name": name, "descriptor": "(" + "".join(arguments) + ")" + result,
                            "modifiers": modifiers})
        else:
            match = re.match(r"(.+?)\s+(.+)$", rest)
            if not match:
                continue
            descriptor = java_descriptor(match.group(1), package, imports,
                                         wildcard_imports, type_variables, available)
            for variable in split_top_level(match.group(2)):
                name = variable.split("=", 1)[0].strip()
                if re.fullmatch(r"\w+", name):
                    fields.append({"name": name, "descriptor": descriptor,
                                   "modifiers": modifiers})
    return {
        "descriptor": "L" + package.replace(".", "/") + "/" + simple_name + ";",
        "kind": kind, "superclass": superclass, "interfaces": interfaces,
        "fields": sorted(fields, key=lambda item: (item["name"], item["descriptor"])),
        "methods": sorted(methods, key=lambda item: (item["name"], item["descriptor"])),
        "source": source_name.replace("\\", "/"),
    }


def inventory(source_root: Path, classes: list[str], package_name: str | None,
              source_id: str) -> dict:
    paths = [source_root / Path(*name.split(".")).with_suffix(".java") for name in classes]
    if package_name:
        paths.extend(sorted((source_root / Path(*package_name.split("."))).glob("*.java")))
    unique = sorted(set(paths))
    available = {path.relative_to(source_root).with_suffix("").as_posix().replace("/", ".")
                 for path in source_root.rglob("*.java")}
    parsed = [parse_java(path.read_text(encoding="utf-8"),
                         path.relative_to(source_root).as_posix(), available)
              for path in unique]
    return {"schema": SCHEMA, "source": source_id,
            "classes": sorted(parsed, key=lambda item: item["descriptor"])}


def self_test() -> int:
    sample = """
package java.test; import java.io.Closeable;
public class Sample<T> extends Base implements Closeable, Runnable {
  public static final int ONE = 1, TWO = 2;
  protected String[] names;
  public Sample(int value) {}
  public <R> R map(final T value, String... names) throws Exception { return null; }
  private void hidden() {}
  public class Nested { public void ignored() {} }
}
"""
    parsed = parse_java(sample, "java/test/Sample.java")
    assert parsed["superclass"] == "Ljava/test/Base;"
    assert parsed["interfaces"] == ["Ljava/io/Closeable;", "Ljava/lang/Runnable;"]
    assert [field["name"] for field in parsed["fields"]] == ["ONE", "TWO", "names"]
    assert any(method["descriptor"] == "(I)V" for method in parsed["methods"])
    assert any(method["descriptor"] == "(Ljava/lang/Object;[Ljava/lang/String;)Ljava/lang/Object;"
               for method in parsed["methods"])
    print("dexvm API-19 surface self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--class", dest="classes", action="append", default=[])
    parser.add_argument("--package")
    parser.add_argument("--source-id", default="android-4.4.4_r2.0.1")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", type=Path)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    if arguments.source_root is None or (not arguments.classes and not arguments.package):
        parser.error("pass --source-root and at least one --class or --package")
    result = inventory(arguments.source_root, arguments.classes, arguments.package,
                       arguments.source_id)
    rendered = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if arguments.check:
        if arguments.check.read_text(encoding="utf-8") != rendered:
            raise SystemExit(f"API-19 surface is stale: {arguments.check}")
        return 0
    if arguments.output:
        arguments.output.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
