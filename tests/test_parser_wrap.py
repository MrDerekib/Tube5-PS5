# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import runpy
import unittest

validate = runpy.run_path(str(pathlib.Path(__file__).parents[1] / "tools/check-parser-wrap.py"))["validate"]
LISTING = """0000000000001000 <av_fast_malloc>:
 1000: jmp 1010 <fast_malloc>

0000000000001010 <fast_malloc>:
 10f6: callq 5000 <__wrap_posix_memalign>
 10fc: testl %eax, %eax
 113c: callq 5000 <__wrap_posix_memalign>
 1142: testl %eax, %eax

0000000000002000 <ff_h2645_packet_split>:
 2120: callq 6000 <__wrap_av_mallocz>
 2126: testq %rax, %rax

0000000000003000 <av_free>:
 3000: jmp 7000 <__wrap_free>

0000000000003100 <av_freep>:
 3100: jmp 7000 <__wrap_free>

0000000000003200 <av_buffer_default_free>:
 3200: jmp 8000 <__wrap_av_free>

0000000000003300 <av_realloc>:
 3300: jmp 9000 <__wrap_realloc>

"""


class ParserWrapTests(unittest.TestCase):
    def test_accepts_only_pinned_allocation_and_release(self):
        validate(LISTING)

    def test_changed_callsites_fail_closed(self):
        for old, new in (("10fc:", "10fd:"), ("1142:", "1143:"), ("2126:", "2127:")):
            with self.subTest(site=old), self.assertRaises(ValueError):
                validate(LISTING.replace(old, new))

    def test_missing_or_bypassed_cleanup_fails_closed(self):
        for symbol in ("av_free", "av_freep", "av_buffer_default_free", "av_realloc"):
            with self.subTest(symbol=symbol), self.assertRaises(ValueError):
                validate(LISTING.replace(f"<{symbol}>:", "<other>:"))
        with self.assertRaises(ValueError):
            validate(LISTING.replace("<__wrap_free>", "<free>"))
        with self.assertRaises(ValueError):
            validate(LISTING.replace("<__wrap_realloc>", "<realloc>"))


if __name__ == "__main__":
    unittest.main()
