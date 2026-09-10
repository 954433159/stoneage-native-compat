-- Exercise the new native item callback; no live characters or database.
local values={['等级']=1,['技能点']=0,['当前经验']=12,['HP']=50,['MP']=80,['限制等级']=140}
local saved,consumed=0,0
local slots={[9]=90}
local busy=false
char={check=function(i)return i==1 and 1 or 0 end,
 getInt=function(i,k)assert(i==1);return values[k] or 0 end,
 setInt=function(i,k,v)assert(i==1 and values[k]~=nil,'unexpected character field: '..k);values[k]=v end,
 getWorkInt=function(i,k)if k=='最大HP' then return 900 elseif k=='最大MP' then return 100 elseif k=='摆摊' then return -1 elseif k=='战斗' then return busy and 1 or 0 else return 0 end end,
 getItemIndex=function(i,s)return slots[s] or -1 end,
 getLevelExp=function(i,level)return level<=140 and 1000 or -1 end,
 DelItem=function(i,s)slots[s]=nil;consumed=consumed+1 end,
 complianceParameter=function()end,Skillupsend=function()end,sendStatusString=function()end,
 charSaveFromConnect=function()saved=saved+1 end,TalkToCli=function()end}
item={check=function(i)return i==90 and 1 or 0 end,getInt=function(i,k)assert(k=='序号');return 31020 end}
assert(loadfile('runtime/gmsv/data/ablua/item/explv.lua'))()
AppLevel140Pill(90,1,2,9);assert(consumed==0,'cannot change another target')
busy=true;AppLevel140Pill(90,1,1,9);assert(consumed==0,'cannot use while fighting');busy=false
AppLevel140Pill(90,1,1,53);assert(consumed==0,'must own exact item in supplied slot')
AppLevel140Pill(90,1,1,9)
assert(values['等级']==140 and values['技能点']==417,'1 to 140 awards exactly 139 * 3 points')
assert(values.HP==900 and values.MP==100 and values['当前经验']==0,'refresh and heal actual target')
assert(saved==1 and consumed==1,'consume and save once')
AppLevel140Pill(90,1,1,9);assert(consumed==1 and saved==1,'repeated callback cannot consume twice')
slots[9]=90;AppLevel140Pill(90,1,1,9);assert(consumed==1,'already level 140 keeps the pill')
values['等级']=130;values['技能点']=7;values['限制等级']=135
AppLevel140Pill(90,1,1,9);assert(consumed==1 and values['等级']==130,'respect native level limit')
values['限制等级']=140;AppLevel140Pill(90,1,1,9)
assert(values['技能点']==37 and consumed==2,'preserve existing unspent points')
print('PASS level140 native item: owner, limits, points, heal, consumption, save; no task/rebirth/attribute edits')
