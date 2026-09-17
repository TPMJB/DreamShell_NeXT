"""Execute the frame DMA cache helper with a recording cache-line primitive."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class FramebufferCacheTests(unittest.TestCase):
    def test_only_framebuffer_lines_are_written_back(self):
        video = (ROOT / "src/video.c").read_text()
        start = video.index("static void WritebackFramebuffer(")
        end = video.index("\nstatic void *VideoThread(void *ptr) {", start)
        helper = video[start:end]
        self.assertIn("WritebackFramebuffer(sdl_dc_buftex,", video[end:])
        program = r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
static uintptr_t lines[32768];
static size_t count;
static void dcache_wback_line(void *line) {
    assert(count < sizeof(lines) / sizeof(lines[0]));
    lines[count++] = (uintptr_t)line;
}
""" + helper + r"""
static void check(uintptr_t address, size_t bytes, size_t expected,
                  uintptr_t first, uintptr_t last) {
    count = 0;
    WritebackFramebuffer((const void *)address, bytes);
    assert(count == expected);
    if (!expected) return;
    assert(lines[0] == first && lines[count - 1] == last);
    for (size_t i = 0; i < count; ++i) {
        assert((lines[i] & 31) == 0);
        assert(lines[i] < address + bytes && lines[i] + 32 > address);
        if (i) assert(lines[i] == lines[i - 1] + 32);
    }
}
int main(void) {
    /* The real 1024x512 16-bit framebuffer exceeds KOS's whole-cache cutoff. */
    check(0x8c600000, 1024 * 512 * 2, 32768, 0x8c600000, 0x8c6fffe0);
    check(0x8c600013, 90, 4, 0x8c600000, 0x8c600060);
    check(0x8c60001f, 2, 2, 0x8c600000, 0x8c600020);
    check(0x8c600000, 32, 1, 0x8c600000, 0x8c600000);
    check(0x8c600013, 0, 0, 0, 0);
    check(0, 0, 0, 0, 0);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "framebuffer_cache.c"
            binary = Path(temporary) / "framebuffer_cache"
            source.write_text(program)
            subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra",
                            "-Werror", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
