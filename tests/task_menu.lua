-- The target moved task overview from 1004 (now newcomer survey) to 1003.
local packet
local e=setmetatable({},{__index=_G})
e.char={getWorkInt=function()return 9090 end}
e.lssproto={windows=function(...)packet={...}end}
assert(loadfile('runtime/gmsv/data/ablua/npc/querymission/querymission.lua','t',e))()
e.data();e.npcindex=90;e.NewQueryMissionTalked(1)
assert(packet[2]==1003,'task query must open target task overview, not newcomer survey 1004')
assert(packet[4]==2000 and packet[6]:match('^100|%d+|'),'native reset price/list/route preserved')
local f=assert(io.open('client/lua/win/1003.xy','rb'));local s=f:read('*a');f:close()
local steps={81,94,66,5,9,35,71,13,26}
for i=#steps,1,-1 do local p=#s-math.floor(#s*steps[i]/100);s=s:sub(p+1):gsub('.',function(c)return string.char(255-c:byte())end)..s:sub(1,p):reverse() end
local function noop()end
local c=setmetatable({},{__index=_G})
c.sa={getServerGold=noop,getIntegerToken=function(v,n)local a={};for t in (v..'|'):gmatch('(.-)|')do a[#a+1]=t end;return tonumber(a[n]) or 0 end}
c.win=setmetatable({getTaskUseFlg=function()return false end},{__index=function()return noop end})
c.sp=setmetatable({},{__index=function()return noop end})
assert(load(s,'@target/win/1003.xy','t',c))();c.WindowInit(packet[6])
assert(c.timeGlod==100 and c.resetTask[4]==true and #c.taskTitleNow==3,'real target task parser must receive original native list')
print('PASS actual native task menu 113 -> target 1003 parser, preserving task/reset IDs')
