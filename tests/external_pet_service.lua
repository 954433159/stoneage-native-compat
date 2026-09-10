-- Exercise actual generated service callback, never live roles/DB.
local file=assert(io.open('native/external-pet-service.lua'));local module=file:read('*a');file:close()
local roles={[1]={name='qa-alpha',account='qa-local',money=30000,pets={}},
             [2]={name='qa-beta',account='qa-local',money=30000,pets={}}}
local pets,packets,notices={}, {}, {}
local serial=100;local failCreate=false
local function role(i)return assert(roles[i])end
char={check=function(i)return (roles[i] or pets[i]) and 1 or 0 end,
    getChar=function(i,k)
        if roles[i] then return k=='账号' and roles[i].account or roles[i].name end
        return k=='唯一编号' and pets[i].uid or pets[i].name
    end,
    getInt=function(i,k)
        if roles[i] then assert(k=='石币');return roles[i].money end
        if k=='宠ID' then return pets[i].id elseif k=='宠技位' then return 7 else return 1 end
    end,
    setInt=function(i,k,v)assert(k=='石币');role(i).money=v end,
    getWorkInt=function(i,k)assert(k=='对象');return 99 end,
    getCharPet=function(i,slot)return role(i).pets[slot] or -1 end,
    getPetSkill=function(i,slot)return pets[i].skills[slot] or -1 end,
    setPetSkill=function(i,slot,skill)pets[i].skills[slot]=skill end,
    sendStatusString=function()end,
    TalkToCli=function(i,_,text)notices[#notices+1]=text end,
    AddPetTempNo=function(i,id,level)
        if failCreate then return -1 end
        assert(level==1);local slot
        for s=0,4 do if not role(i).pets[s] then slot=s;break end end
        assert(slot~=nil);serial=serial+1
        pets[serial]={id=id,name='native-pet',uid='unique-'..serial,skills={[0]=1,[1]=2}}
        role(i).pets[slot]=serial;return serial
    end}
lssproto={windows=function(...)packets[#packets+1]={...}end}
petskill={getPetskillArray=function(id)return id end,check=function()return 1 end,
    getChar=function(id,k)return k=='名称' and ('skill'..id) or 'native skill description' end}
AppExternalPetCatalog={{id=5191,name='传说佩露夏',skills={1,2,700},pending=''},
                       {id=777,name='佩露夏',skills={1,2,700},pending=''}}
assert(load(module))()
local function open(talker,id)AppExternalPetWindow(10,talker,1,0,'R|'..(id or 5191)..'|')end
local function adopt(talker)open(talker);AppExternalPetWindow(10,talker,10,0,'1');AppExternalPetWindow(10,talker,11,1,'')end
adopt(1);assert(role(1).money==20000 and pets[role(1).pets[0]].id==5191)
AppExternalPetWindow(10,1,11,1,'');assert(role(1).money==20000 and not role(1).pets[1],'duplicate adoption')
assert(role(2).money==30000 and not role(2).pets[0],'role ownership')
failCreate=true;adopt(1);assert(role(1).money==20000,'creation failure cannot debit');failCreate=false
role(1).money=9999;adopt(1);assert(role(1).money==9999 and not role(1).pets[1],'insufficient money')
role(1).money=30000
for slot=1,4 do role(1).pets[slot]=role(1).pets[0] end
adopt(1);assert(role(1).money==30000,'full pets cannot debit')
for slot=1,4 do role(1).pets[slot]=nil end
open(1);AppExternalPetWindow(10,1,10,0,'2');AppExternalPetWindow(10,1,12,0,'1')
AppExternalPetWindow(10,1,13,0,'1');AppExternalPetWindow(10,1,14,1,'')
local pet=role(1).pets[0]
assert(pets[pet].skills[2]==700 and role(1).money==29000,'teach native allowed skill')
AppExternalPetWindow(10,1,14,1,'');assert(role(1).money==29000,'duplicate teaching')
pets[pet].skills[2]=nil
open(1);AppExternalPetWindow(10,1,10,0,'2');AppExternalPetWindow(10,1,12,0,'1');AppExternalPetWindow(10,1,13,0,'1')
pets[pet].uid='replaced-pet'
AppExternalPetWindow(10,1,14,1,'');assert(role(1).money==29000 and not pets[pet].skills[2],'changed UID')
for slot=2,6 do pets[pet].skills[slot]=100+slot end
open(1);AppExternalPetWindow(10,1,10,0,'2');AppExternalPetWindow(10,1,12,0,'1');AppExternalPetWindow(10,1,13,0,'1');AppExternalPetWindow(10,1,14,1,'')
assert(role(1).money==29000 and pets[pet].skills[2]==102,'full skills cannot overwrite or debit')
open(1);AppExternalPetWindow(10,1,10,0,'1');role(1).name='different-character'
AppExternalPetWindow(10,1,11,1,'');assert(role(1).money==29000 and not role(1).pets[1],'stale index after role switch')
local n=#packets;open(1,1016);assert(#packets==n,'quarantined species cannot be adopted')
print('PASS real pet service callback: adoption, teaching, duplicate/full/balance/UID/role guards')
