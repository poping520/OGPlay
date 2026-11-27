import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from validate_devices import MAX_BYTES, MAX_PRESETS, PresetError, SCHEMA, load_preset, validate_catalog, validate_fields


class DevicePresetTests(unittest.TestCase):
    def setUp(self):
        self.text = (ROOT / "data/devices/gt-i9300.toml").read_text(encoding="utf-8")
        self.good = load_preset(ROOT / "data/devices/gt-i9300.toml")

    def test_catalog(self):
        values = validate_catalog(ROOT / "data/devices")
        self.assertEqual([v["id"] for v in values], sorted(v["id"] for v in values))
        self.assertGreaterEqual(len(values), 1)

    def test_closed_keys_and_types_at_every_level(self):
        def visit(schema, path=()):
            for key, rule in schema.items():
                for mode in ("missing", "wrong_type"):
                    value = copy.deepcopy(self.good)
                    table = value
                    for part in path:
                        table = table[part]
                    if mode == "missing":
                        del table[key]
                    else:
                        table[key] = []
                    with self.subTest(path=path, key=key, mode=mode), self.assertRaises(PresetError):
                        validate_fields(value, SCHEMA)
                if isinstance(rule, dict):
                    visit(rule, path + (key,))
            value = copy.deepcopy(self.good)
            table = value
            for part in path:
                table = table[part]
            table["typo"] = 1
            with self.assertRaises(PresetError):
                validate_fields(value, SCHEMA)
        visit(SCHEMA)

    def test_numeric_bounds_and_bool(self):
        for group, fields in [(None, SCHEMA), *[(k, v) for k, v in SCHEMA.items() if isinstance(v, dict)]]:
            for key, rule in fields.items():
                if isinstance(rule, dict) or rule[0] is not int:
                    continue
                for number in [True, float(rule[1]), rule[1] - 1, rule[2] + 1]:
                    value = copy.deepcopy(self.good)
                    (value if group is None else value[group])[key] = number
                    with self.subTest(group=group, key=key, number=number), self.assertRaises(PresetError):
                        validate_fields(value, SCHEMA)
                for number in [rule[1], rule[2]]:
                    value = copy.deepcopy(self.good)
                    (value if group is None else value[group])[key] = number
                    validate_fields(value, SCHEMA)

    def test_text_boundaries(self):
        for text in ["", " ", " padded", "a\n", "a\x7f", "中" * 43]:
            value = copy.deepcopy(self.good)
            value["label"] = text
            with self.subTest(text=text), self.assertRaises(PresetError):
                validate_fields(value, SCHEMA)
        value = copy.deepcopy(self.good)
        value["label"] = "中" * 42 + "ab"  # Exactly 128 UTF-8 bytes.
        validate_fields(value, SCHEMA)
        for text in ["", " m0", "m0\t", "机型", "a" * 92]:
            value = copy.deepcopy(self.good)
            value["product"]["model"] = text
            with self.subTest(property=text[:8]), self.assertRaises(PresetError):
                validate_fields(value, SCHEMA)
        value = copy.deepcopy(self.good)
        value["product"]["model"] = "a" * 91  # Exactly PROP_VALUE_MAX - 1 bytes.
        validate_fields(value, SCHEMA)

    def test_bad_files(self):
        fingerprint = "samsung/m0xx/m0:4.4.4/KTU84Q/I9300XXUGNG3:user/release-keys"
        invalid = [
            self.text.replace('id = "gt-i9300"', 'id = "../escape"'),
            self.text.replace('id = "gt-i9300"', 'id = "other"'),
            self.text.replace('schema = 1', 'schema = 1\nschema = 1'),
            self.text.replace('version_release = "4.4.4"', 'version_release = "5.0"'),
            self.text.replace('cpu_abi = "armeabi-v7a"', 'cpu_abi = "arm64-v8a"'),
            self.text.replace(fingerprint, fingerprint.replace("m0xx", "m0")),
            self.text.replace(fingerprint, fingerprint.replace(":4.4.4/", ":4.3/")),
            self.text.replace(fingerprint, fingerprint.replace(":user/", ":eng/")),
            self.text.replace(fingerprint, fingerprint.replace("KTU84Q", "KTU 84Q")),
            self.text.replace('id = "KTU84Q"', 'id = "KTU84P"'),
            self.text.replace('version_incremental = "I9300XXUGNG3"', 'version_incremental = "I9300XXUGNG4"'),
            self.text.replace('id = "KTU84Q"', 'id = "KTU 84Q"').replace(fingerprint, fingerprint.replace("KTU84Q", "KTU 84Q")),
            self.text.replace('midr = "0x413fc090"', 'midr = "0x4130c090"'),
            self.text.replace('midr = "0x413fc090"', 'midr = "0x413FC090"'),
            self.text.replace('midr = "0x413fc090"', 'midr = "413fc090"'),
            self.text.replace('renderer = "Mali-400 MP"', 'renderer = "Mali-400 MP\\n"'),
            self.text + "\n[extra]\nunknown = 1\n",
            "#" * (MAX_BYTES + 1),
        ]
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "gt-i9300.toml"
            for text in invalid:
                path.write_text(text, encoding="utf-8")
                with self.subTest(text=text[:60]), self.assertRaises(PresetError):
                    load_preset(path)
            path.write_bytes(b"\xff")
            with self.assertRaises(PresetError):
                load_preset(path)

    def test_missing_empty_and_oversize_catalogs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for path in [root, root / "missing"]:
                with self.assertRaises(PresetError):
                    validate_catalog(path)
            for i in range(MAX_PRESETS + 1):
                (root / f"p{i}.toml").write_text("", encoding="utf-8")
            with self.assertRaisesRegex(PresetError, "exceeds"):
                validate_catalog(root)

    def test_cli_failure_is_nonzero(self):
        with tempfile.TemporaryDirectory() as temp:
            result = subprocess.run([sys.executable, str(ROOT / "tools/validate_devices.py"), "--devices", temp], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"empty catalog", result.stderr)


if __name__ == "__main__":
    unittest.main()
