-- An App NPC must enter the same personal storage UI, never the legacy SQL
-- writer. This catches the old separate 15-slot warehouse and stale callbacks.
local packet,profile= nil,4
local function forbidden()error('legacy SQL/asset path reached')end
char={getFd=function(i)assert(i==10);return 3 end,
 getInt=function()return 2005 end,getChar=forbidden,getWorkInt=function()return 44 end}
net={getloginmark=function(fd)assert(fd==3);return profile end}
sasql={query=forbidden}
lssproto={windows=function(role,kind,buttons,sequence,object,data)
 assert(role==10);packet={kind,buttons,sequence,object,data} end}
assert(loadfile('runtime/gmsv/data/ablua/npc/pool/poolitem.lua'))()
ShowTalked(20,10,1)
assert(packet and packet[1]==1199 and packet[5]=='personal-item', 'NPC must open personal storage entry')
packet=nil;ShowWindowTalked(20,10,'C|9|',1)
assert(packet and packet[1]==1199,'stale old NPC transfer must only reopen the personal view')
print('PASS NPC personal warehouse routing, no legacy SQL or asset writes')
