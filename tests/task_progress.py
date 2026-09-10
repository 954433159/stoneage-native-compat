"""Production-optimized real native task sender; no live service/player writes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'native/overrides/gmsv/lssproto_serv.c').read_text(encoding='utf8')
start = source.index('void lssproto_EventID_send(')
sender = source[start:source.index('\n}',start)+2]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHAR_ENDEVENT 68
#define CHAR_EVENTID 18
#define LSSPROTO_EVENTID_SEND 809
static char PersonalKey[4096], fields[2][2048], progress[256]="4-1|2-1|";
static int words[32], count;
int CONNECT_getCharaindex(int fd){assert(fd==5);return 7;}
int CHAR_CHECKINDEX(int i){return i==7;}
void CONNECT_getCdkey(int fd,char *s,int n){s[0]=0;}
char *getRandDefaultKey(int fd){return "";}
int CHAR_getInt(int i,int key){assert(i==7 && key>=68 && key<100);return words[key-68];}
char *CHAR_getChar(int i,int key){assert(i==7 && key==18);return progress;}
void CHAR_setChar(int i,int key,char *s){assert(i==7 && key==18);strcpy(progress,s);}
int StrReplace(char *s,char *from,char *to){assert(0);return 0;}
int util_mkstring(int fd,char *buffer,char *s){assert(count<2);strcpy(fields[count++],s);return strlen(s);}
void util_mkint(int fd,char *buffer,int n){}
void util_SendMesg(int fd,int fn,char *buffer){assert(fd==5 && fn==809);}
'''
main = r'''
int main(void){
 for(int n=0;n<32;n++)words[n]=(int)(UINT32_C(1)<<n);
 words[0]=16;words[1]=18304;words[2]=96;
 lssproto_EventID_send(5);
 assert(count==2 && strcmp(fields[1],"4-1|2-1|")==0);
 char *next=fields[0];
 for(int n=0;n<32;n++){
  char *end;long got=strtol(next,&end,10);
  if(got!=words[n] || *end!='|'){fprintf(stderr,"task word %d: expected %d, got %ld (%s)\n",n,words[n],got,next);return 1;}
  next=end+1;
 }
 assert(*next==0);
 puts("PASS actual native task sender: all 1024 completed flags and active progress survive production fortify");
}
'''
with tempfile.TemporaryDirectory(prefix='stone-task-progress-') as temporary:
    path=Path(temporary)/'task.c';binary=Path(temporary)/'task'
    path.write_text(harness+sender+main,encoding='utf8')
    subprocess.run(['cc','-O2','-D_FORTIFY_SOURCE=3',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
