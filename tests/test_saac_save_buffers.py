"""Actual SAAC save functions under 8 MiB stack; no server or player files.

Run in Ubuntu with python3 -B tests/test_saac_save_buffers.py.
SAAC_BUFFER_SOURCE=vendor/saac replays the untouched pre-fix implementation.
Only filesystem/protocol boundaries are stubbed, in a separate translation
unit so the optimizer cannot eliminate the native character lookup buffers.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / os.environ.get("SAAC_BUFFER_SOURCE", "native/overrides/saac")


def section(source, begin, end, *, last=False):
    start = source.rindex(begin) if last else source.index(begin)
    return source[start:source.index(end, start)]


PREFIX = r'''
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include "main.h"
#include "char.h"
#include "util.h"
#include "saacproto_serv.h"
#define SPACE '|'
#define SPACESTRING "|"
#define IS_2BYTEWORD(a) ((char)0x80 <= (a) && (a) <= (char)0xff)
char *getGSName(int ti);
static int findBlankCharIndex(char *id);
static int makeSaveCharString(char *out, int outlen, char *nm, char *opt, char *info);
extern int fixture_writes, fixture_reply, fixture_slot, fixture_reads;
extern char *fixture_saved;
'''

BOUNDARIES = r'''
#include <assert.h>
#include "main.h"
#include "char.h"
int fixture_writes, fixture_reply, fixture_slot = -1, fixture_reads;
char *fixture_saved;
char *getGSName(int ti) { return "fixture-server"; }
int lockUser(char *gs, char *id, char *name, char *password, int lock,
             char *result, int resultlen, char *data, int datalen,
             char *process, char *deadline) { assert(!lock); return 0; }
int loadCharOne(char *id, int num, char *out, int capacity) {
    assert(capacity == CHARDATASIZE);
    ++fixture_reads;
    out[0] = 0;
    return -1; /* A genuinely new test account has no character file. */
}
int saveCharOne(char *id, int num, char *text) {
    ++fixture_writes; fixture_slot = num;
    fixture_saved = strdup(text); assert(fixture_saved);
    return 0;
}
void saacproto_ACCharSave_send(int fd, char *result, char *data, int message) {
    assert(message == 17);
    fixture_reply = !strcmp(result, SUCCESSFUL) ? 1 : -1;
}
'''

DRIVER = r'''
/* Match the live 1 MiB read buffer in main(), keeping pages live across save.
 * No stack-limit increase, disabled protection, or invented gameplay state. */
__attribute__((noinline)) static void dispatch_save(char *info, int slot) {
    volatile unsigned char input[CHARDATASIZE];
    for (size_t i = 0; i < sizeof(input); i += 4096) input[i] = 37;
    charSave(3, "buffer-fixture", "testalpha", "0|23|1", info, 0, 17, slot);
    for (size_t i = 0; i < sizeof(input); i += 4096) assert(input[i] == 37);
}

#ifndef FIXTURE_STACK_CHAIN
static void check_text(void) {
    char out[512];
    const char *want = "石\\z器\\c\\y|村\\n庄|DATAEND=ok\\nuid=pet\\yone";
    assert(makeSaveCharString(out, sizeof(out), "石|器,\\", "村\n庄", "DATAEND=ok\nuid=pet\\one") == 0);
    assert(!strcmp(out, want));
}

static void check_capacity(int delta) {
    /* Escaped result is exactly six bytes: A\\z|B|. */
    const char *want = "A\\z|B|";
    size_t length = strlen(want), capacity = length + 1 + delta;
    unsigned char guard[32]; memset(guard, 0x5a, sizeof(guard));
    int result = makeSaveCharString((char *)guard + 1, capacity, "A|", "B", "");
    assert(guard[0] == 0x5a);
    for (size_t i = capacity + 1; i < sizeof(guard); ++i) assert(guard[i] == 0x5a);
    if (delta >= 0) { assert(result == 0); assert(!strcmp((char *)guard + 1, want)); }
    else assert(result < 0);
}

static void check_large_plain(void) {
    size_t length = CHARDATASIZE - 6;
    char *info = malloc(length + 1), *out = malloc(CHARDATASIZE);
    assert(info && out); memset(info, 'x', length); info[length] = 0;
    assert(makeSaveCharString(out, CHARDATASIZE, "n", "o", info) == 0);
    assert(strlen(out) == length + 4 && !memcmp(out + 4, info, length + 1));
    free(info); free(out);
}
#endif

static void check_large_escaped(void) {
    size_t length = CHARDATASIZE / 2 + 10;
    char *info = malloc(length + 1); assert(info);
    memset(info, '|', length); memcpy(info, "DATAEND=", 8); info[length] = 0;
    dispatch_save(info, 0);
    assert(fixture_reply == -1 && !fixture_writes && !fixture_reads);
    free(info);
}

#ifndef FIXTURE_STACK_CHAIN
static void check_long_field(void) {
    size_t length = CHARDATASIZE + 4096;
    char *name = malloc(length + 1), *out = malloc(CHARDATASIZE);
    assert(name && out); memset(name, 'n', length); name[length] = 0;
    assert(makeSaveCharString(out, CHARDATASIZE, name, "o", "DATAEND=") < 0);
    free(name); free(out);
}

static void check_utf8_edge(void) {
    char out[32];
    const char *want = "石|村|器\\z";
    size_t capacity = strlen(want) + 1;
    assert(makeSaveCharString(out, capacity, "石", "村", "器|") == 0);
    assert(!strcmp(out, want));
    assert(makeSaveCharString(out, capacity - 1, "石", "村", "器|") < 0);
}
#endif

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "new")) {
        dispatch_save("CHAR=1\nDATAEND=complete\n", -1);
        assert(fixture_reply == 1 && fixture_writes == 1 && fixture_slot == 0);
        assert(fixture_reads == MAXCHAR_PER_USER + 1);
        assert(!strcmp(fixture_saved, "testalpha|0\\z23\\z1|CHAR=1\\nDATAEND=complete\\n"));
        free(fixture_saved);
    } else if (!strcmp(argv[1], "existing")) {
        dispatch_save("DATAEND=complete", 1);
        assert(fixture_reply == 1 && fixture_writes == 1 && fixture_slot == 1 && !fixture_reads);
        assert(!strcmp(fixture_saved, "testalpha|0\\z23\\z1|DATAEND=complete"));
        free(fixture_saved);
    } else if (!strcmp(argv[1], "escaped")) check_large_escaped();
#ifndef FIXTURE_STACK_CHAIN
    else if (!strcmp(argv[1], "text")) check_text();
    else if (!strcmp(argv[1], "exact")) check_capacity(0);
    else if (!strcmp(argv[1], "room")) check_capacity(1);
    else if (!strcmp(argv[1], "short")) check_capacity(-1);
    else if (!strcmp(argv[1], "large")) check_large_plain();
    else if (!strcmp(argv[1], "long")) check_long_field();
    else if (!strcmp(argv[1], "utf8")) check_utf8_edge();
#endif
    else assert(0);
    printf("PASS SAAC save buffers: %s\n", argv[1]);
    return 0;
}
'''


@unittest.skipUnless(sys.platform == "linux" and shutil.which("cc"), "Run under Ubuntu with cc")
class SaacSaveBufferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="stone-saac-save-")
        cls.addClassCleanup(cls.temp.cleanup)
        temporary = Path(cls.temp.name)
        text = (SOURCE / "char.c").read_text(encoding="utf-8", errors="surrogateescape")
        body = PREFIX
        body += section(text, "static void getCharNameFromString(char *str, char *out)\n{", "\nstatic void getCharOptionFromString(")
        body += section(text, "static int makeSaveCharString(char *out, int outlen,", "\nint getCharIndexByName(", last=True)
        body += section(text, "int getCharIndexByName(char *id, char *charname)\n{", "\nstatic int unlinkCharFile(")
        body += section(text, "#ifdef _NewSave\nint charSave(", "\nvoid  charListCallback(")
        (temporary / "fixture.c").write_text(body + DRIVER, encoding="utf-8", errors="surrogateescape")
        (temporary / "boundaries.c").write_text(BOUNDARIES, encoding="utf-8")
        cls.binary = temporary / "fixture"
        command = ["cc", "-O2", "-g", "-std=gnu99", "-D_GNU_SOURCE", "-D_FORTIFY_SOURCE=2",
                   "-fstack-clash-protection", "-fstack-protector-strong", "-fstack-usage",
                   "-ffunction-sections", "-fdata-sections", "-Wno-implicit-function-declaration",
                   "-I" + str(SOURCE / "include"), "-I/usr/include/mariadb",
                   str(temporary / "fixture.c"), str(temporary / "boundaries.c"),
                   str(SOURCE / "util.c"), "-Wl,--gc-sections", "-o", str(cls.binary)]
        result = subprocess.run(command, cwd=temporary, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        # Match production's sole makeSaveCharString call site: adding boundary
        # callers changes GCC's inlining decision and can hide the 8 MiB crash.
        cls.stack_binary = temporary / "stack-fixture"
        command[-1] = str(cls.stack_binary)
        command.insert(1, "-DFIXTURE_STACK_CHAIN")
        result = subprocess.run(command, cwd=temporary, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        print("SAAC source:", SOURCE, flush=True)
        for path in temporary.glob("*.su"):
            for line in path.read_text().splitlines():
                if any(name in line for name in ("charSave", "makeSaveCharString", "getCharIndexByName", "makeEscapeString", "dispatch_save")):
                    print("stack-usage:", line, flush=True)

    def run_case(self, name):
        import resource

        def limits():
            resource.setrlimit(resource.RLIMIT_STACK, (8 * 1024 * 1024, 8 * 1024 * 1024))
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

        binary = self.stack_binary if name in ("new", "existing", "escaped") else self.binary
        result = subprocess.run([str(binary), name], cwd=self.temp.name,
                                preexec_fn=limits, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, f"{name}: exit={result.returncode}\n{result.stdout}{result.stderr}")

    def test_new_character_default_stack(self): self.run_case("new")
    def test_existing_character_default_stack(self): self.run_case("existing")
    def test_native_escaping_bytes(self): self.run_case("text")
    def test_exact_capacity_includes_nul(self): self.run_case("exact")
    def test_capacity_with_room(self): self.run_case("room")
    def test_one_byte_too_small_is_rejected(self): self.run_case("short")
    def test_large_unescaped_record_is_not_truncated(self): self.run_case("large")
    def test_escaping_overflow_never_writes_or_acks_success(self): self.run_case("escaped")
    def test_overlong_field_is_rejected(self): self.run_case("long")
    def test_utf8_at_exact_capacity(self): self.run_case("utf8")


if __name__ == "__main__":
    unittest.main(verbosity=2)
