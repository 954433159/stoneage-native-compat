"""Run the actual common native pet factory used by Lua AddPet/egg rewards."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CreatedPetObjectTests(unittest.TestCase):
    def test_new_pet_not_mistaken_for_a_world_pet(self):
        source = (ROOT/'native/overrides/gmsv/char/pet.c').read_text(encoding='utf8')
        begin = source.index('int PET_initCharOneArray(Char *ch)')
        end = source.index('\nint PET_createPetFromCharaIndex(', begin)
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define _PET_FUSION
#define CHAR_FUNCTABLENUM 19
#define CHAR_LOOPFUNC 6
#define CHAR_LOOPFUNCTEMP1 16
#define CHAR_MAILMODE 0
#define CHAR_FUSIONBEIT 1
#define CHAR_FUSIONRAISE 2
#define CHAR_LOOPINTERVAL 3
#define CHAR_PETMAIL_NONE 0
#define CHAR_WORKOBJINDEX 0
typedef struct { int data[8],workint[4],skills[7],equipment[7];
    char uid[64],owner[64];
    struct {char string[64];} charfunctable[CHAR_FUNCTABLENUM];
    void *functable[CHAR_FUNCTABLENUM]; } Char;
static Char allocated;
static int allocation_result;
void strcpysafe(char *dst,size_t size,const char *src){snprintf(dst,size,"%s",src);}
void *getFunctionPointerFromName(const char *s){
    assert(!strcmp(s,"PET_CheckIncubateLoop"));return (void *)123;
}
int CHAR_initCharOneArray(Char *ch){allocated=*ch;return allocation_result;}
'''
        harness += source[begin:end]
        harness += r'''
int main(void){
    for(int mail=0;mail<2;mail++)for(int fusion=0;fusion<2;fusion++){
        Char ch={0}; /* Native ENEMY_createPet defaults have work object zero. */
        ch.data[CHAR_MAILMODE]=mail;ch.data[CHAR_FUSIONBEIT]=fusion;
        ch.data[CHAR_FUSIONRAISE]=fusion?9:0;ch.data[4]=39;ch.data[5]=718;
        ch.workint[1]=8;ch.workint[2]=99;
        strcpy(ch.uid,"generated-pet-unique-001");strcpy(ch.owner,"qa-beta");
        for(int i=0;i<7;i++){ch.skills[i]=i+4;ch.equipment[i]=i+200;}
        Char before=ch;allocation_result=203;
        assert(PET_initCharOneArray(&ch)==203);
        assert(allocated.workint[CHAR_WORKOBJINDEX]==-1);
        assert(allocated.workint[1]==8 && allocated.workint[2]==99);
        if(fusion)before.data[CHAR_LOOPINTERVAL]=60000;
        assert(!memcmp(allocated.data,before.data,sizeof(before.data)));
        assert(!memcmp(allocated.skills,before.skills,sizeof(before.skills)));
        assert(!memcmp(allocated.equipment,before.equipment,sizeof(before.equipment)));
        assert(!strcmp(allocated.uid,before.uid) && !strcmp(allocated.owner,before.owner));
        assert(!strcmp(allocated.charfunctable[CHAR_LOOPFUNC].string,mail?"PETMAIL_Loop":""));
        assert(!strcmp(allocated.charfunctable[CHAR_LOOPFUNCTEMP1].string,fusion?"PET_CheckIncubateLoop":""));
        /* Mail/follow world loaders allocate their object after this factory;
           object slot zero remains valid once explicitly assigned by them. */
        allocated.workint[CHAR_WORKOBJINDEX]=0;
        assert(allocated.workint[CHAR_WORKOBJINDEX]>=0);
        allocation_result=-1;assert(PET_initCharOneArray(&ch)==-1);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='stone-created-pet-') as temporary:
            path=Path(temporary)
            (path/'pet.c').write_text(harness,encoding='utf8')
            subprocess.run(['cc','-O2','-Wall',str(path/'pet.c'),'-o',str(path/'pet')],
                           check=True,capture_output=True)
            result=subprocess.run([str(path/'pet')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)


if __name__=='__main__':
    unittest.main()
