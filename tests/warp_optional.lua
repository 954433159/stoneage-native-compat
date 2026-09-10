dofile('runtime/gmsv/data/ablua/warpmap.lua')
local calls = {}
lssproto = {}
char = {getFd=function() return 5 end}
other = {CallFunction=function(name) calls[#calls+1]=name; return 1 end}
assert(NewMapBattleInfo(0,1006,13,21)==0)
assert(#calls==0)
assert(NewMapBattleInfo(0,60501,1,1)==0)
assert(calls[1]=='showmmexp', 'MM-map behavior must remain available')
config = {getGameservername=function() return 'LocalStone' end}
char = {getInt=function(_,field) return field=='等级' and 1 or 1006 end,
        newMessageToCli=function() end}
assert(FreeWarpMap(0,32017,1,1)==0, 'level restriction must remain enforced')
assert(FreeWarpMap(0,1000,1,1)==1, 'normal map transfer must continue')
print('PASS: disabled battle-HUD binding does not abort map transfer; map restrictions preserved')
