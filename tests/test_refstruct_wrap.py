# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("refstruct_check", Path(__file__).resolve().parents[1] / "tools/check-refstruct-wrap.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class RefstructWrapTests(unittest.TestCase):
    def test_guard_accepts_only_pinned_callsite_and_all_release_paths(self):
        listing = """0000000000001000 <ff_refstruct_pool_get>:
 1092: e8 callq 0x50 <__wrap_av_malloc>
 1099: 48 testq %rax, %rax
 1100: e8 callq 0x60 <__wrap_av_free>

0000000000001200 <pool_return_entry>:
 1210: e8 callq 0x60 <__wrap_av_free>

0000000000001300 <refstruct_pool_uninit>:
 1310: e8 callq 0x60 <__wrap_av_free>
"""
        check.validate(listing)
        for changed in (listing.replace("1099:", "109a:"),
                        listing.replace("<__wrap_av_malloc>", "<av_malloc>"),
                        listing.replace("<__wrap_av_free>", "<av_free>"),
                        listing.replace("<pool_return_entry>", "<unexpected>")):
            with self.assertRaises(ValueError):
                check.validate(changed)


if __name__ == "__main__":
    unittest.main()
