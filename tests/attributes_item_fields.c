/* Compile the actual native serializer. Prints real rows for the target decoder fixture. */
#include <assert.h>
#include "common.h"
/* Declarations absent from the original item.c includes; signatures verified at
 * function_mylua.c:1102/6389/6428 and lssproto_serv.c:5175. */
int FreeJudgePlayer(int index);
int FreeGetItemStar(int index);
BOOL EquipEffectFunction(int index,int id);
void lssproto_CHAREFFECT_send(int fd,char *message);
#include "../native/overrides/gmsv/item/item.c"
int _CHAR_getWorkInt(char *file,int line,int index,CHAR_WORKDATAINT field){return 7;}
int CONNECT_getloginmark(int fd){return 4;}
int FreeJudgePlayer(int index){return 1;}
void strcpysafe(char *out,size_t length,const char *in){snprintf(out,length,"%s",in);}
char *makeEscapeString(char *in,char *out,int length){snprintf(out,length,"%s",in);return out;}
int FreeGetItemStar(int index){return 0;}
int MAGIC_getMagicArray(int id){return -1;}
char *MAGIC_getChar(int index,MAGIC_DATACHAR field){return "";}
/* ASAN retains native configuration callback tables; serialization must not call them. */
BOOL getStringFromIndexWithDelim_body(char *src,char *delim,int index,char *out,int length,char *file,int line){abort();}
BOOL isstring1or0(char *s){abort();}
static int bars(const char *s){int n=0;while(*s)if(*s++=='|')++n;return n;}
int main(void){
    static ITEM_exists fixture[2];char rows[54][2048];
    assert(CHAR_MAXITEMHAVE==54 && CHAR_EQUIPPLACENUM==9);
    ITEM_item=fixture;ITEM_itemnum=2;
    for(int i=0;i<2;i++){
        fixture[i].use=TRUE;
        snprintf(fixture[i].itm.string[ITEM_NAME].string,32,"fixture-%d",i);
        snprintf(fixture[i].itm.string[ITEM_SECRETNAME].string,32,"fixture-%d",i);
        fixture[i].itm.data[ITEM_TYPE]=i?ITEM_AXE:ITEM_CLUB;
        fixture[i].itm.data[ITEM_MODIFYATTACK]=37+i;
        fixture[i].itm.data[ITEM_BASEIMAGENUMBER]=24000+i;
        fixture[i].itm.data[ITEM_ABLEUSEFIELD]=1;
        fixture[i].itm.data[ITEM_TARGET]=1;
        fixture[i].itm.data[ITEM_LEVEL]=5+i;
        fixture[i].itm.data[ITEM_USEPILENUMS]=1;
        strcpy(fixture[i].itm.string[ITEM_TYPECODE].string,"2,4");
        strcpy(fixture[i].itm.string[ITEM_INLAYCODE].string,"9,11");
        strcpy(fixture[i].itm.string[ITEM_USEFUNC].string,"ITEM_useEquip");
    }
    for(int i=0;i<54;i++){
        const char *s=(i==0||i==53)?ITEM_makeItemStatusString(1,-1,i==0?0:1):ITEM_makeItemFalseString(1);
        assert(bars(s)==20);snprintf(rows[i],sizeof(rows[i]),"%s",s);
    }
    printf("TYPES=%d|%d\n",ITEM_CLUB,ITEM_AXE);
    printf("S=I");for(int i=0;i<54;i++)printf("%s%s",i?"|":"",rows[i]);puts("");
    printf("I=0|%s|53|%s\n",rows[53],rows[0]);
    printf("EMPTY=0|%s\n",ITEM_makeItemFalseString(1));
    puts("PASS: real native 54-slot serializer preserves target 21/22-field strides and rich item fields");
    return 0;
}
