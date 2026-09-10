"""Replay the real native W formatter and S sender against the target's 6-field stride."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PetSkillWire(unittest.TestCase):
    def test_real_native_sender_preserves_every_skill_and_empty_slot(self):
        source = (ROOT/'native/overrides/gmsv/char/char.c').read_text(encoding='utf8')
        start = source.index("        case 'w':")
        branch = source[start:source.index('\n#ifdef _PROFESSION_SKILL', start)]
        source = (ROOT/'native/overrides/gmsv/lssproto_serv.c').read_text(encoding='utf8')
        start = source.index('void lssproto_S_send(')
        sender = source[start:source.index('\n}', start)+2]
        harness = r'''
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if __has_include("app_pet_skill_wire.h")
#include "app_pet_skill_wire.h"
#endif
#define FALSE 0
#define CHAR_MAXPETHAVE 5
#define CHAR_MAXPETSKILLHAVE 7
#define CHAR_CHECKINDEX(i) ((i)>=0)
#define PETSKILL_CHECKINDEX(i) ((i)>=0)
#define STATUSSENDDELIMITER "|"
#define arraysizeof(a) (sizeof(a)/sizeof((a)[0]))
#define print printf
#define LSSPROTO_S_SEND 46
enum {PETSKILL_NAME,PETSKILL_COMMENT,PETSKILL_ID,PETSKILL_FIELD,PETSKILL_TARGET};
static char CHAR_statusSendBuffer[8192], PersonalKey[8192], captured[8192];
static int ids[7]={1,2,700,-1,-1,-1,-1}, virtual_fd;
int CHAR_getCharPet(int i,int slot){assert(slot==3);return 4;}
int CHAR_getPetSkill(int i,int slot){return ids[slot];}
int PETSKILL_getPetskillArray(int id){return id;}
int PETSKILL_getInt(int id,int field){return field==PETSKILL_ID?id:field==PETSKILL_FIELD?1:id==2?5:6;}
char *PETSKILL_getChar(int id,int field){
    if(field==PETSKILL_COMMENT)return "real-description";
    return id==1?"attack":id==2?"guard":"blood";
}
void strcpysafe(char *d,size_t n,const char *s){snprintf(d,n,"%s",s);}
char *makeEscapeString(char *s,char *d,int n){snprintf(d,n,"%s",s);return d;}
int stone_app_status(int fd,int protocol,const char *s){if(virtual_fd)strcpy(captured,s);return virtual_fd;}
int CONNECT_checkfd(int fd){return 1;}
void CONNECT_getCdkey(int fd,char *s,int n){s[0]=0;}
char *getRandDefaultKey(int fd){return "";}
int util_mkstring(int fd,char *b,char *s){strcpy(captured,s);return 0;}
void util_mkint(int fd,char *b,int n){}
void util_SendMesg(int fd,int protocol,char *b){}
void stone_app_role_roster(int fd){}
char *native_w(int index,char *category){
    int strlength=0;
    switch(tolower(category[0])){
@@BRANCH@@
    }
    return "";
}
@@SENDER@@
static const char *field(int n){
    static char result[256];const char *p=captured,*end;
    for(int i=0;i<n;i++){p=strchr(p,'|');assert(p);p++;}
    end=strchr(p,'|');assert(end);size_t length=end-p;assert(length<sizeof(result));
    memcpy(result,p,length);result[length]=0;return result;
}
int main(void){
    for(virtual_fd=0;virtual_fd<2;virtual_fd++){
        lssproto_S_send(9,native_w(0,"W3"));
        /* Target 0x874c7e multiplies the skill index by 6, then reads
         * id/field/target/name/memo/extra; 0x874bd0 enables 7+4 slots. */
        assert(atoi(field(1))==1 && !strcmp(field(4),"attack"));
        assert(atoi(field(7))==2 && !strcmp(field(10),"guard"));
        assert(atoi(field(13))==700 && !strcmp(field(16),"blood"));
        assert(atoi(field(18))==0 && !strcmp(field(65),""));
        ids[1]=-1;lssproto_S_send(9,native_w(0,"W3"));
        assert(!strcmp(field(10),"") && atoi(field(13))==700);ids[1]=2;
    }
    virtual_fd=0;lssproto_S_send(9,"P|unchanged|");assert(!strcmp(captured,"P|unchanged|"));
    puts("PASS real W/S sender: 6-field skills, holes, 4 empty passive slots, primary/controlled role routes");
}
'''.replace('@@BRANCH@@', branch).replace('@@SENDER@@', sender)
        with tempfile.TemporaryDirectory(prefix='stone-pet-wire-') as directory:
            c, exe = Path(directory)/'wire.c', Path(directory)/'wire'
            c.write_text(harness, encoding='utf8')
            subprocess.run(['cc', '-O1', '-g', '-fsanitize=address,undefined',
                            '-I'+str(ROOT/'native/include'), str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
