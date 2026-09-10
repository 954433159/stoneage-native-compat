-- Exercise the real onboarding script with the native Lua runtime.
dofile('runtime/gmsv/data/ablua/newplayer.lua')
data()
npcindex = 90
local flag, now306, items, pets, last, events = 100, 0, {}, {}, nil, {}
char = {
    getInt=function(_,key)
        if key=='新玩家旗标' then return flag end
        if key=='原图像号' then return 100010 end
        return 0
    end,
    setInt=function(_,key,value) if key=='新玩家旗标' then flag=value end end,
    getWorkInt=function() return 91 end,
    Additem=function(_,id) items[#items+1]=id; return #items end,
    AddPet=function(_,id,level) pets[#pets+1]={id,level}; return #pets end,
}
npc = {CheckNowEvent=function() return now306 end}
lssproto = {windows=function(...) last={...} end}
other = {CallFunction=function(name, file, args) events[#events+1]={name,file,args} end}
FreeNewPlayer(0)
assert(last[2]=='选择框', '20260805 window 1005 is stall history, not onboarding')
assert(last[4]==3 and last[5]==91, 'selection must route to the original onboarding NPC')
WindowTalked(90,0,3,8,'')
assert(flag==100 and #items==0 and #pets==0, 'cancel must not grant anything')
WindowTalked(90,0,3,0,'3')
assert(flag==100 and #items==0 and #pets==0, 'invalid selection must not grant anything')
WindowTalked(90,0,3,0,'1')
assert(events[#events][1]=='Talked' and #pets==0, 'guide choice must preserve original handler')
WindowTalked(90,0,3,0,'2')
assert(flag==0 and #items==5 and #pets==2, 'starter choice must execute original rewards')
assert(pets[1][1]==1 and pets[2][1]==2484 and pets[1][2]==1, 'birthplace/appearance pet selection changed')
WindowTalked(90,0,3,0,'2')
assert(#items==5 and #pets==2, 'replayed selection must not duplicate rewards')
flag=100; now306=1
WindowTalked(90,0,3,0,'2')
assert(#items==5 and #pets==2, 'existing tutorial progress must remain protected')
print('PASS: native starter UI routes valid choices, cancellation and duplicate protection')
