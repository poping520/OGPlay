import importlib.util
import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).parents[2] / "tools" / "diagnostics" / "align_host_threads.py"
SPEC = importlib.util.spec_from_file_location("align_host_threads", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class AlignHostThreadsTests(unittest.TestCase):
    def test_aligns_decimal_windbg_hex_and_lldb_tid_without_symbol_guessing(self):
        snapshot = {
            "executions": [
                {"host_tid": 4660, "guest_tid": 7, "context_token": 9,
                 "path": "guest_call", "name": "InvokeA32GuestCall"},
                {"host_tid": 42, "guest_tid": 8, "context_token": 10,
                 "path": "clone_thread", "name": "RunAndroidArmGuestThread"},
                {"host_tid": 99, "guest_tid": 9, "context_token": 11,
                 "path": "guest_call", "name": "unknown"},
            ]
        }
        stacks = """0  Id: 1111.1234 Suspend: 1 Teb: 0
module!known+0x1
thread #2, tid = 42, 0x0000 module+0x2
module+0x2
"""
        aligned = MODULE.align(snapshot, stacks)
        self.assertEqual(aligned[0]["stack_status"], "matched")
        self.assertIn("module!known", aligned[0]["stack_lines"][1])
        self.assertEqual(aligned[1]["stack_status"], "matched")
        self.assertEqual(aligned[2]["stack_status"], "unavailable")
        self.assertNotIn("symbol", aligned[2])


if __name__ == "__main__":
    unittest.main()
