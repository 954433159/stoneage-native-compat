-- Execute the complete native samenu.lua; only character/network APIs are fixtures.
local path=arg[1] or 'runtime/gmsv/data/ablua/samenu.lua'
local e=setmetatable({},{__index=_G});e._G=e
local roles,role,packets,changes,compliance
local fields={'体力','腕力','耐力','速度'}
local function reset()
    roles={}
    for i=1,2 do roles[i]={['体力']=1000,['腕力']=1000,['耐力']=0,['速度']=0,
        ['技能点']=10,['职业类别']=0,['死亡']=0,['战斗']=0,['交易状态']=0,['摆摊']=-1,fd=7} end
    role=1;packets={};changes=0;compliance=0
end
e.net={getCharaindex=function(fd) assert(fd==7);return role end}
e.char={check=function(i)return roles[i]and 1 or 0 end,
    getFd=function(i)return roles[i].fd end,
    getInt=function(i,k)assert(roles[i][k]~=nil,k);return roles[i][k]end,
    setInt=function(i,k,v)assert(v>=0 and v<=2147483647 and v%1==0);roles[i][k]=v;changes=changes+1 end,
    getWorkInt=function(i,k)assert(roles[i][k]~=nil,k);return roles[i][k]end,
    getFlg=function(i,k)assert(k=='死亡');return roles[i][k]end,
    complianceParameter=function(i)compliance=compliance+1 end,
    sendStatusString=function(i,k)assert(k=='P');packets[#packets+1]={'P',i}end,
    Skillupsend=function(i)packets[#packets+1]={'SKUP',i,roles[i]['技能点']}end,
    newMessageToCli=function()end}
e.other={getString=function(s,d,n)
    local a={};for v in (s..d):gmatch('(.-)'..d)do a[#a+1]=v end;return a[n]or'' end,
    atoi=function(s)return math.floor(tonumber(s:match('^[+-]?%d+'))or 0)end}
local f=assert(io.open(path,'rb'));local src=f:read('*a');f:close()
assert(load(src,'@'..path,'t',e))()
local function apply(s)e.FreeNewSaMenu(7,21,s)end
local function budget(i)local a=100*roles[i]['技能点'];for _,k in ipairs(fields)do a=a+roles[i][k]end;return a end
local function reject(s,why)
    local before=budget(role);local n=changes;apply(s)
    assert(changes==n and budget(role)==before,why)
end
reset();local original=budget(1);apply('1|2|3|4|')
assert(roles[1]['体力']==1100 and roles[1]['腕力']==1200 and roles[1]['耐力']==300 and roles[1]['速度']==400)
assert(roles[1]['技能点']==0 and budget(1)==original and compliance==1)
assert(#packets==2 and packets[1][1]=='P' and packets[2][1]=='SKUP' and packets[2][3]==0)
-- First expected original-resource failure, after verifying its valid request/refresh behavior.
reset();roles[1]['战斗']=1;reject('1|0|0|0|','bulk points must reject battle state')
for _,entry in ipairs({{'死亡',1},{'交易状态',1},{'交易状态',2},{'交易状态',3},{'摆摊',1},{'摆摊',2}})do
    reset();roles[1][entry[1]]=entry[2];reject('1|0|0|0|','blocked '..entry[1])
end
for _,s in ipairs({'1|0|0|0','1|0|0|0|role2|','1x|0|0|0|','1.5|0|0|0|',
    '1e2|0|0|0|','-1|0|0|0|',' 1|0|0|0|','|0|0|0|','11|0|0|0|',
    '2147483648|0|0|0|','999999999999999999999999999999|0|0|0|','0|0|0|0|'})do
    reset();reject(s,'strict four unsigned integers / budget: '..s)
end
reset();roles[1].fd=8;reject('1|0|0|0|','fd must own the current role')
reset();roles[1]['体力']=2147483640;reject('1|0|0|0|','native int overflow')
for _,entry in ipairs({{1,4,20000},{2,2,20000},{2,3,20000},{3,2,20000},{3,3,20000},{3,4,40000}})do
    reset();roles[1]['职业类别']=entry[1];roles[1][fields[entry[2]]]=entry[3]-100
    local n={0,0,0,0};n[entry[2]]=1;local request=table.concat(n,'|')..'|'
    local b=budget(1);apply(request);assert(roles[1][fields[entry[2]]]==entry[3] and budget(1)==b)
    reject(request,'profession cap '..entry[1]..'/'..entry[2])
end
reset();role=2;apply('1|0|0|0|')
assert(roles[1]['体力']==1000 and roles[2]['体力']==1100 and packets[1][2]==2)
print('PASS: actual bulk-point menu validates payload, state, current fd, budget and native profession caps; P/SKUP retained')
