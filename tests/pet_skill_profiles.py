"""Compile the actual native skill setup and HP/MP resolution functions."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.index('int '+name+'(')
    end = source.index('\n}', start)+2
    return source[start:end]


class NativeSkillProfiles(unittest.TestCase):
    def test_real_skill_math_and_legacy_defaults(self):
        skills = (ROOT/'native/overrides/gmsv/battle/pet_skill.c').read_text(encoding='utf-8')
        battle = (ROOT/'native/overrides/gmsv/battle/battle_event.c').read_text(encoding='utf-8')
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_pet_skill_options.h"
#define TRUE 1
#define FALSE 0
#define print printf
#define min(a,b) ((a)<(b)?(a):(b))
enum {CHAR_HP,CHAR_MP,CHAR_WHICHTYPE,CHAR_WORKMAXHP,CHAR_WORKFIXSTR,
CHAR_WORKFIXTOUGH,CHAR_WORKFIXDEX,CHAR_WORKATTACKPOWER,CHAR_WORKDEFENCEPOWER,
CHAR_WORKQUICK,CHAR_WORKBATTLECOM1,CHAR_WORKBATTLECOM2,CHAR_WORKBATTLECOM3,CHAR_WORKBATTLEMODE};
enum {BATTLE_COM_GUARD=100,BATTLE_COM_S_DAMAGETOHP2,BATTLE_COM_S_DAMAGETOHP,
BATTLE_COM_S_MPDAMAGE,BATTLE_COM_S_GYRATE,BATTLE_CHARMODE_C_OK,CHAR_TYPEPLAYER,CHAR_TYPEPET,CHAR_TYPEENEMY};
#define PETSKILL_OPTION 0
#define CHAR_P_STRING_HP 1
#define CHAR_P_STRING_MP 2
static int state[2][20],react=0;
static const char *option;
static int CHAR_getInt(int p,int k){return state[p][k];}
static int CHAR_getWorkInt(int p,int k){return state[p][k];}
static void CHAR_setInt(int p,int k,int v){state[p][k]=v;}
static void CHAR_setWorkInt(int p,int k,int v){state[p][k]=v;}
#define CHAR_SETWORKINT_LOW(p,k,v) CHAR_setWorkInt(p,k,v)
#define CHAR_GETWORKINT_LOW(p,k) CHAR_getWorkInt(p,k)
static char *PETSKILL_getChar(int a,int k){return (char *)option;}
static int BATTLE_GetDamageReact(int p){return react;}
static void CHAR_send_P_StatusString(int p,int k){}
static int getStringFromIndexWithDelim(const char *s,const char *sep,int n,char *out,size_t size){
 for(int i=1;i<n;i++){s=strchr(s,*sep);if(!s)return 0;s++;}
 const char *end=strchr(s,*sep);size_t len=end?(size_t)(end-s):strlen(s);
 if(len>=size)return 0;memcpy(out,s,len);out[len]=0;return 1;
}
'''
        for name in ('PETSKILL_DamageToHp2', 'PETSKILL_DamageToHp', 'PETSKILL_MpDamage', 'PETSKILL_Gyrate'):
            harness += '\n'+function(skills, name)+'\n'
        for name in ('BATTLE_S_DamageToHp2', 'BATTLE_S_DamageToHp', 'BATTLE_S_MpDamage'):
            harness += '\n'+function(battle, name)+'\n'
        start = battle.index('    if (opt == BATTLE_COM_S_DAMAGETOHP2)')
        end = battle.index('\n    gCriper = perCri;', start)
        harness += '\nstatic int actual_attack_math(int attackindex,int opt,int perCri){\n'+battle[start:end]+'\nreturn perCri;\n}\n'
        harness += r'''
int main(void){
 state[0][CHAR_WORKMAXHP]=1000;state[0][CHAR_WORKFIXSTR]=200;
 state[0][CHAR_WORKFIXTOUGH]=100;state[0][CHAR_WORKFIXDEX]=70;
 state[0][CHAR_HP]=899;option="50 hp=90 atk=75 def=-30 critical=30";
 assert(PETSKILL_DamageToHp2(0,1,700,NULL));
 assert(state[0][CHAR_WORKBATTLECOM1]==BATTLE_COM_S_DAMAGETOHP2);
 assert(state[0][CHAR_WORKDEFENCEPOWER]==70);
 assert(actual_attack_math(0,BATTLE_COM_S_DAMAGETOHP2,1000)==1300);
 assert(state[0][CHAR_WORKATTACKPOWER]==350);
 state[0][CHAR_HP]=900;PETSKILL_DamageToHp2(0,1,700,NULL);
 assert(state[0][CHAR_WORKBATTLECOM1]==BATTLE_COM_GUARD);
 state[0][CHAR_HP]=800;assert(BATTLE_S_DamageToHp2(0,0,1,100,700)==50);
 assert(state[0][CHAR_HP]==850);
 state[0][CHAR_HP]=990;assert(BATTLE_S_DamageToHp2(0,0,1,100,700)==10);
 assert(state[0][CHAR_HP]==1000);
 react=1;state[0][CHAR_HP]=700;assert(BATTLE_S_DamageToHp2(0,0,1,100,700)==0);
 assert(state[0][CHAR_HP]==700);react=0;
 option="100";state[0][CHAR_HP]=500;PETSKILL_DamageToHp2(0,1,623,NULL);
 assert(state[0][CHAR_WORKBATTLECOM1]==BATTLE_COM_GUARD);
 state[0][CHAR_HP]=499;state[0][CHAR_WORKDEFENCEPOWER]=100;PETSKILL_DamageToHp2(0,1,623,NULL);
 assert(state[0][CHAR_WORKBATTLECOM1]==BATTLE_COM_S_DAMAGETOHP2 && state[0][CHAR_WORKDEFENCEPOWER]==100);
 actual_attack_math(0,BATTLE_COM_S_DAMAGETOHP2,1000);assert(state[0][CHAR_WORKATTACKPOWER]==240);
 assert(BATTLE_S_DamageToHp2(0,0,1,100,623)==100);
 option="-30|30";PETSKILL_DamageToHp(0,1,717,NULL);assert(state[0][CHAR_WORKATTACKPOWER]==260);
 state[0][CHAR_HP]=500;assert(BATTLE_S_DamageToHp(0,0,1,200,717)==60);
 option="-10|50";PETSKILL_MpDamage(0,1,716,NULL);assert(state[0][CHAR_WORKATTACKPOWER]==220);
 state[1][CHAR_WHICHTYPE]=CHAR_TYPEPLAYER;state[1][CHAR_MP]=100;
 assert(BATTLE_S_MpDamage(0,0,1,20,716)==50 && state[1][CHAR_MP]==50);
 option="攻%+15 防%-35";PETSKILL_Gyrate(0,1,718,NULL);
 assert(state[0][CHAR_WORKATTACKPOWER]==230 && state[0][CHAR_WORKDEFENCEPOWER]==65);
 assert(stone_pet_option("50 hp=9000", " hp=",50,1,100)==50);
 assert(stone_pet_option("50 hp=90garbage", " hp=",50,1,100)==50);
 puts("PASS native skill threshold, real damage/heal/MP math, defence and preserved legacy profile");
 return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='stone-pet-skill-') as temporary:
            directory=Path(temporary); source=directory/'skills.c'; binary=directory/'skills'
            source.write_text(harness, encoding='utf-8')
            subprocess.run(['cc','-std=c99','-O1','-g','-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all','-I'+str(ROOT/'native/include'),str(source),'-o',str(binary)],
                           check=True, capture_output=True)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout.strip())


if __name__=='__main__':
    unittest.main()
