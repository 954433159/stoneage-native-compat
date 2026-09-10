-- Feed the actual native fixture packets to the unmodified target 1123 Lua.
-- This is a protocol/resource fixture, not a GUI or external-server test.
local function read(path) local f=assert(io.open(path,'rb'));local s=f:read('*a');f:close();return s end
local function field(s,n)
    local p=1
    for i=1,n do local q=s:find('|',p,true);if i==n then return s:sub(p,q and q-1 or #s) end;if not q then return '' end;p=q+1 end
end
local function decode(path)
    local raw=read(path);local s=raw;local steps={81,94,66,5,9,35,71,13,26}
    local function inv(v) return (v:gsub('.',function(c)return string.char(255-c:byte())end)) end
    for i=#steps,1,-1 do local cut=#s-math.floor(#s*steps[i]/100);s=inv(s:sub(cut+1))..s:sub(1,cut):reverse() end
    assert(utf8.len(s),'target resource is not strict UTF-8')
    local encoded=s
    for _,v in ipairs(steps) do local cut=math.floor(#encoded*v/100);encoded=encoded:sub(cut+1):reverse()..inv(encoded:sub(1,cut)) end
    assert(encoded==raw,'target resource does not round-trip');return s
end
local noop=function()end
local packets,prompts={},{}
local input='101'
local cons={pc={item=1},item={useFlag=1,useType=2,name=3,damage=4,graNo=5,memo=6,trn=7,level=8,magicname=9,inslaytype=10,inslaycode=11}}
local e=setmetatable({},{__index=_G});e._G=e;e.package={path='',loaded={}}
e.math=setmetatable({mod=math.fmod},{__index=math})
e.require=function(name) if name=='cons' then return cons elseif name=='common' then return {calcPetData=function()return 0 end} end;error(name) end
e.other={md5=function()return '0123456789abcdef0123456789abcdef' end}
e.win=setmetatable({getWinSizeX=function()return 800 end,getWinSizeY=function()return 600 end,
    getIndexWN=function()return 0x4b000001 end,getIdWN=function()return -1 end,GetInputMsg=function()return input end},{__index=function()return noop end})
e.sa=setmetatable({getLuaPath=function()return 'client/lua' end,getStringToken=field,
    getIntegerToken=function(s,n)return tonumber(field(s,n):match('^[+-]?%d+')) or 0 end,
    initItemDispExt=function(...)e.item_draw={...}end,StockPromptBufferLine=function(s)prompts[#prompts+1]=s end},{__index=function()return noop end})
e.char={getPc=function()return 1 end,getItem=function(_,_,key)return key==cons.item.useFlag and 1 or 0 end}
e.lssproto={windows=function(sequence,object,select,data)assert(sequence==0x4b000001 and object==-1 and select==0);packets[#packets+1]=data end}
local source=decode('client/lua/win/1123.xy');assert(load(source,'@target/1123.xy','t',e))();e.main()
local output=os.getenv('STONE_CHECK_OUTPUT_DIR') or 'evidence'
local function receive(kind) e.WindowInit(read(output..'/services-market-'..kind..'.txt')) end
receive('empty');assert(e.xycheck==1 and e.types==1 and #e.fenlei==3 and #e.shujv==0)
receive('list');assert(e.types==2 and #e.shujv==1 and e.shujv[1].name=="原生'测试斧" and e.shujv[1].jiage==303 and e.shujv[1].num==3 and e.shujv[1].itemid=='1')
e.selectItemFunc(1);assert(packets[#packets]=='X|2|1|0|')
e.goumai(1);e.goumaiok(1);assert(packets[#packets]=='B|2|1|')
receive('item-detail');assert(e.iteminfo.name=="原生'测试斧" and e.iteminfo.graNo==20033 and e.iteminfo.memo=='攻 +37' and e.iteminfo.magicname=='恢复精灵')
assert(e.item_draw[4]=="原生'测试斧" and e.item_draw[7]=='攻 +37')
receive('pet-list');assert(e.types==1 and #e.shujv==1 and e.shujv[1].pic==61159)
receive('pet-mine');assert(e.doing==2 and #e.shujv==1 and e.shujv[1].pic==61159)
receive('pet-detail');assert(e.iteminfo.name=="原生'测试宠" and e.iteminfo.hp==590 and e.iteminfo.atk==98 and e.iteminfo.maxSkill==2 and e.iteminfo.graNo==100001)
assert(e.iteminfo.skillName[1]=='攻击' and e.iteminfo.skillName[2]=='防御' and e.iteminfo.blessHp==1 and e.iteminfo.NoblessHp==577 and #e.iteminfo.beidong==4)
receive('mine-expired');assert(e.mytype==1 and e.doing==2 and e.shujv[1].issell==0)
e.getback(1);e.getbackok(1);assert(packets[#packets]=='G|2|1|')
receive('mine-sold');assert(e.mytype==2 and e.doing==3 and e.shujv[1].issell==1)
e.doingbs(2);assert(packets[#packets]=='M|1|');e.doingbs(3);assert(packets[#packets]=='M|2|')
-- The target lays out two columns at a 72-pixel row pitch. A fifth row
-- overlaps the pager (also reproduced with ten real GUI-created listings).
receive('page-one');assert(e.page==1 and e.allpage==2 and #e.shujv==8,
    'target exchange must contain four rows/eight entries per page')
local draws={}
e.sa.StockDispBuffer=function(...) draws[#draws+1]={...} end
e.sa.getTextLength=function(s)return #tostring(s)*8 end
e.sp=setmetatable({},{__index=function()return noop end})
e.bit={band=function(a,b)return a&b end,lshift=function(a,b)return a<<b end}
cons.config={joy_trg=1};e.char.getConfig=function()return 0 end
e.showinfo=false;e.buyFlag=false;e.getFlag=false;e.ServerWindowType()
local pager,lastrow
for _,draw in ipairs(draws) do
    if draw[6]=='gotopage' then pager=math.min(pager or math.huge,draw[2]) end
    if draw[6]=='selectItemFunc' then lastrow=math.max(lastrow or -math.huge,draw[2]) end
end
assert(pager and lastrow and pager-lastrow>=36,'real target row buttons overlap the pager')
receive('page-two');assert(e.page==2 and e.allpage==2 and #e.shujv==4)
e.gotopage(3);assert(packets[#packets]=='P|2|1|')
e.mypage=1;e.myallpage=2;e.doing=3;e.mygotopage(2);assert(packets[#packets]=='A|2|2|')
e.changfenlei(3);e.doing=0;e.changfenlei(1);assert(packets[#packets]=='L|1|')
e.choosepet(0)
for d=1,3 do e.choosetype(d);e.shangjia();assert(packets[#packets]=='U|1|101|'..d..'|0|0|') end
e.openItemflg3Info(9);e.choosetype(2);e.shangjia();assert(packets[#packets]=='U|1|101|2|-1|9|')
input='99';local before=#packets;e.shangjia();assert(#packets==before and #prompts>0)
-- The original text conflicts with the actual duration buttons; retain both
-- pieces of source evidence and execute the real callback ids above.
assert(source:find('"168 小时"',1,true) and source:find('" 48 小时"',1,true) and source:find('" 24 小时"',1,true))
assert(source:find('"choosetype",3',1,true) and source:find('12小时上架手续费为成交价的5%',1,true))
local out=assert(io.open(output..'/services-market-client-requests.txt','wb'));out:write(table.concat(packets,'\n'),'\n');out:close()

-- Execute the actual native resource used by the C adapter's strict hook.
local role={['转数']=0,['等级']=80,['地图号']=5543}
local pet={['宠ID']=717,['安全锁']=0,['名字']='测试宠'}
local item={['序号']=0,['合成']=0,['名称']='测试斧',['显示名']='测试斧',['使用函数名']='',['编码']='native-code',['字段']='2000'}
local checkused=0
local trade=setmetatable({},{__index=_G})
trade.char={getInt=function(i,k)return (i==1 and role or pet)[k] or 0 end,getChar=function(i,k)return pet[k] or '' end,newMessageToCli=noop,TalkToCli=noop}
trade.item={getInt=function(_,k)return item[k] or 0 end,getChar=function(_,k)return item[k] or '' end}
trade.other={atoi=function(s)return tonumber(s) or 0 end,time=function()return 1000 end}
trade.sasql={query=function()return 1 end,free_result=noop,store_result=noop,num_rows=function()return 1 end,fetch_row=noop,data=function(n)return n==3 and checkused or 0 end}
assert(load(read('runtime/gmsv/data/ablua/trade.lua'),'@native/trade.lua','t',trade))()
assert(trade.FreeTradePet(1,2)==0 and trade.FreeTradeItem(1,3)==0)
role['等级']=49;assert(trade.FreeTradePet(1,2)==1 and trade.FreeTradeItem(1,3)==1);role['等级']=80
for _,floor in ipairs({41011,41012,40030,40034}) do role['地图号']=floor;assert(trade.FreeTradeItem(1,3)==1) end;role['地图号']=5543
pet['宠ID']=4516;assert(trade.FreeTradePet(1,2)==1);pet['宠ID']=717;pet['安全锁']=1;assert(trade.FreeTradePet(1,2)==1);pet['安全锁']=0
pet['名字']='*绑定';assert(trade.FreeTradePet(1,2)==1);pet['名字']='测试宠'
item['使用函数名']='ITEM_MMEXP';assert(trade.FreeTradeItem(1,3)==1);item['使用函数名']=''
item['序号']=22001;item['字段']='500';assert(trade.FreeTradeItem(1,3)==1);item['字段']='2000';checkused=1;assert(trade.FreeTradeItem(1,3)==1)
checkused=0;assert(trade.FreeTradeItem(1,3)==0)
print('PASS market client: actual decoded 1123 L/M/X/I, paging/listing/buy/return callbacks and native trade.lua restrictions')
