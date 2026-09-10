"""Exercise the real saved-pet parser before any map object is allocated."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LoadedPetObjectTests(unittest.TestCase):
    def test_loaded_pet_has_no_map_object_and_preserves_saved_fields(self):
        source = (ROOT / 'native/overrides/gmsv/char/char_base.c').read_text(encoding='utf-8')
        begin = source.index('int CHAR_makePetFromStringToArg(')
        end = source.index('\n#ifdef _PERSONAL_FAME', begin)
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOOL int
#define TRUE 1
#define FALSE 0
#define CHAR_DATAINTNUM 3
#define CHAR_DATACHARNUM 3
#define CHAR_WORKOBJINDEX 0
#define NONCHAR_DELIMITER "|"
#define PETSKILLSERVERSTRING "skill"
#define CHAR_CHECKPETSKILLINDEX(i) ((i)>=0 && (i)<2)
#define fprint(...) ((void)0)
typedef struct { int data[3], workint[2]; struct { char string[128]; } string[3];
                 union { int indexOfPetskill[2]; } unionTable; } Char;
char *CHAR_setintdata[] = {"lv", "hp", "mail"};
char *CHAR_setchardata[] = {"name", "ownt", "uid"};
void CHAR_getDefaultChar(Char *ch, int image) {
    assert(image == 31010); memset(ch, 0, sizeof(*ch));
    ch->unionTable.indexOfPetskill[0] = ch->unionTable.indexOfPetskill[1] = -1;
}
void LodBadPetString(char *s, char *why, int slot) { (void)s; (void)why; (void)slot; }
int getStringFromIndexWithDelim(char *src, char *delim, int n, char *out, size_t size) {
    char *s=src, *e; for(int i=1; i<n; ++i) { s=strstr(s,delim); if(!s) return 0; s+=strlen(delim); }
    if(!*s) return 0; e=strstr(s,delim); size_t len=e?(size_t)(e-s):strlen(s);
    if(len>=size) return 0; memcpy(out,s,len); out[len]=0; return 1;
}
void strcpysafe(char *out, size_t size, char *s) { snprintf(out,size,"%s",s); }
char *makeStringFromEscaped(char *s) { return s; }
int PETSKILL_getPetskillArray(int skill) { return skill == 42 ? 0 : -1; }
'''
        harness += source[begin:end]
        harness += r'''
int main(void) {
    Char ch;
    for(int mail=0;mail<2;++mail) {
        char saved[256]; snprintf(saved,sizeof(saved),"name:TestPet|ownt:testalpha|uid:pet-001|lv:4|hp:79|mail:%d|skill0:42|",mail);
        memset(&ch,0x5a,sizeof(ch));
        assert(CHAR_makePetFromStringToArg(saved,&ch,0)==TRUE);
        assert(ch.workint[CHAR_WORKOBJINDEX]==-1);
        assert(ch.data[0]==4 && ch.data[1]==79 && ch.data[2]==mail);
        assert(!strcmp(ch.string[0].string,"TestPet") && !strcmp(ch.string[1].string,"testalpha"));
        assert(!strcmp(ch.string[2].string,"pet-001") && ch.unionTable.indexOfPetskill[0]==42);
    }
    assert(CHAR_makePetFromStringToArg("name:incomplete|",&ch,0)==-1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='stone-loaded-pet-') as directory:
            path = Path(directory)
            (path / 'pet.c').write_text(harness)
            subprocess.run(['cc', '-O2', '-Wall', str(path / 'pet.c'), '-o', str(path / 'pet')],
                           check=True, capture_output=True)
            result = subprocess.run([str(path / 'pet')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
