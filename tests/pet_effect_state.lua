-- Execute the real server resource. Only native object lookup / wire APIs are
-- substituted; any accidental currency, SQL or asset mutation raises an error.
local fields={ [10]={['战斗特效']=109900,['战斗特效2']=125002},
               [14]={['战斗特效']=109905,['战斗特效2']=0} }
local response
char={check=function(i)return (i==1 or fields[i]) and 1 or 0 end,
 getFd=function(i)assert(i==1);return 7 end,
 getCharPet=function(i,slot)assert(i==1);return fields[slot+10] and slot+10 or -1 end,
 getInt=function(i,key)return assert(fields[i][key]) end}
other={getString=function(s,sep,n)local t={};for p in(s..sep):gmatch('(.-)'..sep)do t[#t+1]=p end;return t[n]or'' end}
lssproto={NewSaMenu=function(fd,index,data)assert(fd==7 and index==19);response=data end}
assert(loadfile('runtime/gmsv/data/ablua/npc/petatterrect/petatterrect.lua'))()
luapetatt(1,'O|')
assert(response=='5|0|109900|125002|1|0|0|2|0|0|3|0|0|4|109905|0|',
 'opening effects must return actual body/aura state for all five slots')
if arg[1] then local f=assert(io.open(arg[1],'wb'));f:write(response);f:close() end
print('PASS native pet effect open reply, sparse slots and actual nonzero effects')
