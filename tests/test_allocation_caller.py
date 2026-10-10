# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "allocation_caller", Path(__file__).parents[1] / "tools/resolve-allocation-caller.py")
caller = importlib.util.module_from_spec(spec)
spec.loader.exec_module(caller)


class AllocationCallerTests(unittest.TestCase):
    symbols = "1000 T before\n2000 T __wrap_posix_memalign\n3000 t after\n4000 D data\n"

    def test_positive_offset(self):
        self.assertEqual(caller.resolve_symbols(self.symbols, 0x1010),
                         (0x3010, "after", 0x0f))

    def test_negative_offset(self):
        self.assertEqual(caller.resolve_symbols(self.symbols, -0x0ff0),
                         (0x1010, "before", 0x0f))

    def test_return_pc_at_function_boundary(self):
        self.assertEqual(caller.resolve_symbols(self.symbols, 0x1000),
                         (0x3000, "__wrap_posix_memalign", 0x0fff))

    def test_missing_anchor_and_out_of_range(self):
        with self.assertRaises(ValueError):
            caller.resolve_symbols("1000 T other", 0)
        with self.assertRaises(ValueError):
            caller.resolve_symbols(self.symbols, -0x2000)


if __name__ == "__main__":
    unittest.main()
