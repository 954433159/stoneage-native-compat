/* Exact CHAR_sendSIToCli source is generated from native source by the audit builder.
 * The target SI fixture models the independently disassembled omitted fields. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
static int profile=4,fd=7,si_count,item_count,status_count;
typedef struct { int use,type;char name[32],alch[32]; } Entry;
static Entry server[54],client[54];
int getfdFromCharaIndex(int index){assert(index==1);return fd;}
int CONNECT_getloginmark(int f){assert(f==fd);return profile;}
void lssproto_SI_send(int f,int from,int to){
    assert(f==fd && from>=0 && from<54 && to>=0 && to<54);++si_count;
    /* Actual target 0x7f5800 swaps useFlag/name but omits type/alch. */
    int use=client[from].use;client[from].use=client[to].use;client[to].use=use;
    char name[32];strcpy(name,client[from].name);strcpy(client[from].name,client[to].name);strcpy(client[to].name,name);
}
BOOL CHAR_sendItemData(int index,int *slots,int count){
    assert(index==1 && count==2 && si_count==1);++item_count;
    for(int i=0;i<count;i++){assert(slots[i]>=0 && slots[i]<54);
        if(server[slots[i]].use)client[slots[i]]=server[slots[i]];else client[slots[i]].use=0;}
    return TRUE;
}
BOOL CHAR_sendStatusString(int index,char *category){
    assert(index==1 && !strcmp(category,"I"));++status_count;
    for(int i=0;i<54;i++)if(i<54 && server[i].use)client[i]=server[i];else client[i].use=0;
    return TRUE;
}
#ifndef ATTR_HELPER
#define ATTR_HELPER "../evidence/attributes-ordinary-before.h"
#endif
#include ATTR_HELPER
static void reset(void){
    memset(server,0,sizeof(server));memset(client,0,sizeof(client));
    profile=4;fd=7;si_count=item_count=status_count=0;
    client[0]=(Entry){1,1,"old axe","ore A"};client[9]=(Entry){1,3,"new club","ore B"};
    server[0]=client[9];server[9]=client[0];
}
int main(void){
    reset();assert(CHAR_sendSIToCli(1,9,0));
    assert(!strcmp(client[0].name,server[0].name));
    assert(client[0].type==server[0].type && "SI must refresh omitted item type");
    assert(!strcmp(client[0].alch,server[0].alch));assert(item_count==1 && status_count==0);
    assert(client[9].type==server[9].type && !strcmp(client[9].alch,server[9].alch));
    reset();memset(&client[9],0,sizeof(client[9]));server[9]=client[0];memset(&server[0],0,sizeof(server[0]));
    assert(CHAR_sendSIToCli(1,0,9));assert(!client[0].use && client[9].use && client[9].type==server[9].type);
    reset();client[53].use=1;assert(!CHAR_sendSIToCli(1,-1,-1));
    assert(status_count==1 && si_count==0 && item_count==0 && !client[53].use);
    assert(client[0].type==server[0].type && client[9].type==server[9].type);
    reset();profile=2;assert(CHAR_sendSIToCli(1,0,9));assert(si_count==1 && item_count==0 && status_count==0);
    assert(!CHAR_sendSIToCli(1,-1,-1));assert(status_count==0);
    reset();fd=-1;assert(!CHAR_sendSIToCli(1,0,9));assert(si_count==0 && item_count==0 && status_count==0);
    puts("PASS: exact native SI helper refreshes both target inventory rows, rejected moves resync; legacy/no-fd behavior retained");
    return 0;
}
