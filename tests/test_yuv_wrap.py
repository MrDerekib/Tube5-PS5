# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import runpy
import unittest

validate = runpy.run_path(str(pathlib.Path(__file__).parents[1] / "tools/check-yuv-wrap.py"))["validate"]
LISTING = """0000000000001000 <SDL_SW_CreateYUVTexture>:
 10c2: callq 2000 <__wrap_SDL_SIMDAlloc_REAL>
 10c8: testq %rax, %rax
 10ff: callq 3000 <__wrap_SDL_SIMDFree_REAL>

0000000000001100 <SDL_SW_DestroyYUVTexture>:
 1110: callq 3000 <__wrap_SDL_SIMDFree_REAL>

"""

class YuvWrapTests(unittest.TestCase):
    def test_pinned_route(self):
        validate(LISTING)

    def test_changed_route_rejected(self):
        for old, new in (("10c8:", "10c9:"),
                         ("<__wrap_SDL_SIMDFree_REAL>", "<SDL_SIMDFree_REAL>"),
                         ("<__wrap_SDL_SIMDAlloc_REAL>", "<SDL_SIMDAlloc_REAL>")):
            with self.subTest(route=old), self.assertRaises(ValueError):
                validate(LISTING.replace(old, new))

if __name__ == "__main__":
    unittest.main()
