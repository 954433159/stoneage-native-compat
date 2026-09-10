-- Actual generated menu/callback; stateful native boundaries, no live player DB.
local file=assert(io.open('runtime/gmsv/data/itemset6.csv','rb'));local ids={}
for line in file:lines()do
 local columns={};for v in (line..','):gmatch('(.-),')do columns[#columns+1]=v end
 if tonumber(columns[17]) then ids[tonumber(columns[17])]=true end
end
file:close()
for id=31000,31003 do assert(ids[id],'reward must be a complete native CSV row: '..id)end
local packets, notices, saved, serial = {}, {}, {}, 100
local roles = {[1]={name='event-qa',account='qa-events',online=130,level=140,rebirth=5,items={},ledger=''},
               [2]={name='other-qa',account='qa-other',online=0,level=1,rebirth=0,items={},ledger=''}}
local objects={};local failCreate=false;local busy=false
local function r(i)return assert(roles[i])end
local now=os.time(); local day=os.date('%Y%m%d',now)
local realTime=os.time;os.time=function(t)return t and realTime(t) or now end
char={check=function(i)return (roles[i] or i==90) and 1 or 0 end,
 getChar=function(i,k)if k=='活动领奖' then return r(i).ledger elseif k=='活动奖箱' then return r(i).box or '' elseif k=='名字' then return r(i).name else assert(k=='账号');return r(i).account end end,
 setChar=function(i,k,v)assert(#v<128);if k=='活动领奖' then r(i).ledger=v else assert(k=='活动奖箱');r(i).box=v end end,
 getInt=function(i,k)local a=r(i);return ({['签到在线时间']=a.online,['等级']=a.level,['转数']=a.rebirth,['地图号']=1000,['石币']=10000})[k] or 0 end,
 getWorkInt=function(i,k)if k=='对象' then return 9090 elseif k=='登陆时间' then return now elseif k=='战斗' then return busy and 1 or 0 elseif k=='摆摊' then return -1 else return 0 end end,
 getFlg=function()return 0 end,getFd=function(i)return i+10 end,
 getItemIndex=function(i,slot)return r(i).items[slot] or -1 end,
 Additem=function(i,id)
  if failCreate then return -1 end
  for slot=9,53 do if not r(i).items[slot] then serial=serial+1;r(i).items[slot]=serial;objects[serial]={id=id,uid='uid-'..serial};return serial end end
  return -1
 end,
 DelItem=function(i,slot)objects[r(i).items[slot]]=nil;r(i).items[slot]=nil end,
 TalkToCli=function(i,_,t)notices[#notices+1]=t end,newMessageToCli=function()end,
 charSaveFromConnect=function(i)saved[i]={ledger=r(i).ledger,items={}};for k,v in pairs(r(i).items)do saved[i].items[k]=v end end,
 setFunctionPointer=function()end,sendStatusString=function()end}
item={getSecretNameFromNumber=function(id)return 'item'..id end,getgraNoFromITEMtabl=function(id)return 24052 end,
 getItemInfoFromNumber=function(id)return 'native reward '..id end,check=function(i)return objects[i] and 1 or 0 end,
 getInt=function(i,k)return k=='序号' and objects[i].id or 1 end,getChar=function(i,k)return objects[i].uid end,
 UpdataItemOne=function()end}
npc={CreateNpc=function()return 90 end}
lssproto={windows=function(...)packets[#packets+1]={...}end,windowsupdate=function(...)packets[#packets+1]={...}end}
other={CallFunction=function()error('welfare must not dispatch to the missing/disabled legacy huodong.lua')end,time=function()return now end,
 getString=function(s,sep,n)local p=1;for i=1,n do local q=s:find(sep,p,true);if i==n then return s:sub(p,q and q-1 or #s) end;if not q then return '' end;p=q+#sep end end}
net={getCharaindex=function(fd)return fd-10 end}
assert(loadfile('runtime/gmsv/data/ablua/samenu.lua'))()
assert(AppEventsCreate,'missing target activity window adapter')
AppEventsCreate();FreeSaMenu(1,34)
assert(packets[#packets][2]==1100 and packets[#packets][6]:match('^L|'),'welfare directory must use the real target parser')
AppEventsOpen(2)
local function claim(role,category,index)AppEventsWindow(90,role,1100,category,'G|'..index..'|')end
claim(1,2,1);assert(saved[1] and r(1).items[9],'qualified daily reward must create and save an item')
local ledger=r(1).ledger;local first=r(1).items[9]
claim(1,2,1);assert(not r(1).items[10] and r(1).ledger==ledger,'duplicate confirmation cannot award again')
r(1).ledger=saved[1].ledger;claim(1,2,1);assert(not r(1).items[10],'normal save/reload cannot award again')
claim(2,2,1);assert(not r(2).items[9],'server must check online time')
claim(2,3,1);assert(not r(2).items[9],'server must check level and rebirth')
for slot=10,53 do r(1).items[slot]=first end
claim(1,2,2);assert(r(1).ledger==ledger,'full 45-slot bag must not consume eligibility')
r(1).items[53]=nil;claim(1,2,2);assert(r(1).items[53] and r(1).items[53]~=first,'third bag page must be usable')
ledger=r(1).ledger;r(1).items[52]=nil;failCreate=true;claim(1,2,3);assert(r(1).ledger==ledger,'allocation failure must not consume reward')
failCreate=false;busy=true;claim(1,2,3);assert(r(1).ledger==ledger,'busy player cannot receive reward');busy=false
for _,bad in ipairs({'G|0|','G|5|','G|-1|','G|1.5|','G|1|extra','G|nan|'})do AppEventsWindow(90,1,1100,2,bad)end
assert(r(1).ledger==ledger,'malformed index/packet must not grant')
r(2).ledger='unknown-existing-state';r(2).online=180;claim(2,2,1)
assert(r(2).ledger=='unknown-existing-state' and not r(2).items[9],'unknown persisted data must fail closed')
now=now+86400;r(1).online=0;AppEventsOpen(1);ledger=r(1).ledger;claim(1,2,1)
assert(r(1).ledger==ledger,'new day cannot borrow yesterday online time')
r(1).online=10;claim(1,2,1);assert(r(1).items[52] and r(1).ledger~=ledger,'new qualified day can receive exactly once')
print('PASS native activity callbacks: real item grant, ownership, save/reload, repeated/full/qualification/invalid-state guards')
