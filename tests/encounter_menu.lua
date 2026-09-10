-- The actual client sends menu 104 with its displayed state (0=start, 1=stop).
dofile('runtime/gmsv/data/ablua/samenu.lua')
local connected, party, state, starts, stops = true, 0, 0, 0, 0
net = {getCharaindex=function(fd) return fd == 7 and connected and 2 or -1 end}
char = {
    check=function(index) return index == 2 and 1 or 0 end,
    getWorkInt=function(index, field) assert(index == 2 and field == '组队'); return party end,
    getCharStayEncount=function(index) assert(index == 2); return state end,
    Encounter=function(index) assert(index == 2); starts=starts+1; state=1 end,
    StopEncounter=function(index) assert(index == 2); stops=stops+1; state=0 end,
}
FreeNewSaMenu(7,104,'0')
assert(state==1 and starts==1, 'encounter button must call the original native encounter loop')
FreeNewSaMenu(7,104,'0')
assert(starts==1 and state==1, 'duplicate start must not toggle the state')
FreeNewSaMenu(7,104,'1')
assert(state==0 and stops==1, 'rest button must stop repeated encounters')
FreeNewSaMenu(7,104,'1')
assert(stops==1, 'duplicate stop must remain stopped')
for _,value in ipairs({'','01','-1','2','bad'}) do FreeNewSaMenu(7,104,value) end
FreeNewSaMenu(8,104,'0'); connected=false; FreeNewSaMenu(7,104,'0'); connected=true
party=2; FreeNewSaMenu(7,104,'0')
assert(starts==1 and stops==1 and state==0, 'invalid input, disconnected clients and party followers must not start')
party=1; FreeNewSaMenu(7,104,'0')
assert(starts==2 and state==1, 'party leaders may start native encounters')
print('PASS: real encounter menu start/stop, duplicate handling and native character ownership')
