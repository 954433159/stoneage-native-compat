"""Actual native pet serialization/parser roundtrip with a zero elder field."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class PetZeroFieldTests(unittest.TestCase):
    def test_zero_elder_does_not_restore_as_minus_one(self):
        source=(ROOT/'native/overrides/gmsv/char/char_base.c').read_text(encoding='utf8')
        begin=source.index('static char CHAR_petdataString[')
        end=source.index('\n#ifdef _PERSONAL_FAME',begin)
        harness=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOOL int
#define TRUE 1
#define FALSE 0
#define CHAR_DATAINTNUM 5
#define CHAR_DATACHARNUM 3
#define CHAR_WORKOBJINDEX 0
#define CHAR_LASTTALKELDER 2
#define CHAR_MAXPETSKILLHAVE 2
#define NONCHAR_DELIMITER "|"
#define PETSKILLSERVERSTRING "psk"
#define CHAR_CHECKPETSKILLINDEX(i) ((i)>=0 && (i)<2)
#define fprint(...) ((void)0)
typedef struct {int data[5],workint[2];struct {char string[128];}string[3];
                union {int indexOfPetskill[2];}unionTable;}Char;
char *CHAR_setintdata[]={"lv","hp","lte","mail","fmlf"};
char *CHAR_setchardata[]={"name","ownt","uid"};
Char actual;
void CHAR_getDefaultChar(Char *ch,int image){
    assert(image==31010);memset(ch,0,sizeof(*ch));
    ch->data[CHAR_LASTTALKELDER]=-1;
    ch->unionTable.indexOfPetskill[0]=ch->unionTable.indexOfPetskill[1]=-1;
}
int CHAR_getInt(int index,int key){assert(index==7);return actual.data[key];}
char *CHAR_getChar(int index,int key){assert(index==7);return actual.string[key].string;}
int CHAR_getPetSkill(int index,int key){assert(index==7);return actual.unionTable.indexOfPetskill[key];}
int PETSKILL_getPetskillArray(int skill){return skill==42?0:-1;}
void strcpysafe(char *out,size_t size,const char *s){snprintf(out,size,"%s",s);}
char *makeEscapeString(char *s,char *out,size_t size){strcpysafe(out,size,s);return out;}
char *makeStringFromEscaped(char *s){return s;}
void LodBadPetString(char *s,char *why,int slot){(void)s;(void)why;(void)slot;}
int getStringFromIndexWithDelim(char *src,char *delim,int n,char *out,size_t size){
    char *s=src,*e;for(int i=1;i<n;i++){s=strstr(s,delim);if(!s)return 0;s+=strlen(delim);}
    if(!*s)return 0;e=strstr(s,delim);size_t len=e?(size_t)(e-s):strlen(s);
    if(len>=size)return 0;memcpy(out,s,len);out[len]=0;return 1;
}
'''
        serializer_end=source.index('\nvoid LodBadPetString(',begin)
        parser_begin=source.index('int CHAR_makePetFromStringToArg(',serializer_end)
        harness+=source[begin:serializer_end]+source[parser_begin:end]
        harness+=r'''
int main(void){
    for(int elder=-1;elder<=1;elder++)for(int mail=0;mail<=1;mail++){
        actual=(Char){0};actual.data[0]=1;actual.data[1]=38;
        actual.data[CHAR_LASTTALKELDER]=elder;actual.data[3]=mail;actual.data[4]=mail;
        strcpy(actual.string[0].string,"NativeNewPet");strcpy(actual.string[1].string,"qa-beta");
        strcpy(actual.string[2].string,"unique-native-001");
        actual.unionTable.indexOfPetskill[0]=42;actual.unionTable.indexOfPetskill[1]=-1;
        char *saved=CHAR_makePetStringFromPetIndex(7);Char restored;
        assert(CHAR_makePetFromStringToArg(saved,&restored,0)==TRUE);
        assert(!memcmp(actual.data,restored.data,sizeof(actual.data)));
        assert(!memcmp(actual.string,restored.string,sizeof(actual.string)));
        assert(!memcmp(&actual.unionTable,&restored.unionTable,sizeof(actual.unionTable)));
    }
    /* Old saves without lte retain the original default and still load. */
    Char old;assert(CHAR_makePetFromStringToArg("name:Old|ownt:qa|uid:old|lv:4|hp:50|",&old,0)==TRUE);
    assert(old.data[CHAR_LASTTALKELDER]==-1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='stone-pet-zero-') as temporary:
            path=Path(temporary);(path/'pet.c').write_text(harness,encoding='utf8')
            built=subprocess.run(['cc','-O2','-Wall',str(path/'pet.c'),'-o',str(path/'pet')],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            result=subprocess.run([str(path/'pet')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)


if __name__=='__main__':
    unittest.main()
