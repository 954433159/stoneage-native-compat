-- Run with the real interpreter: build/lua_reference tests/service_menus.lua [shop.lua]
-- No services, accounts, or database are used. Native API boundaries are stateful fixtures.
local function read(path)
    local f=assert(io.open(path,'rb')); local s=f:read('*a'); f:close(); return s
end
local function env()
    local e=setmetatable({}, {__index=_G}); e._G=e; return e
end
local function script(path,e) assert(load(read(path),'@'..path,'t',e))() end
local function field(s, sep, n)
    local start=1
    for i=1,n do
        local finish=s:find(sep,start,true)
        if i==n then return s:sub(start,finish and finish-1 or #s) end
        if not finish then return '' end
        start=finish+#sep
    end
end
local function client_script(path,e)
    local raw=read(path); local s=raw; local steps={81,94,66,5,9,35,71,13,26}
    local function inv(v) return (v:gsub('.',function(c) return string.char(255-c:byte()) end)) end
    for i=#steps,1,-1 do
        local cut=#s-math.floor(#s*steps[i]/100)
        s=inv(s:sub(cut+1))..s:sub(1,cut):reverse()
    end
    assert(utf8.len(s),'target resource must be strict UTF-8')
    local r=s
    for _,p in ipairs(steps) do local cut=math.floor(#r*p/100); r=r:sub(cut+1):reverse()..inv(r:sub(1,cut)) end
    assert(r==raw,'target resource must round-trip byte for byte')
    assert(load(s,'@'..path,'t',e))()
end
local noop=function() end
local templates={}
for line in read('runtime/gmsv/data/itemset6.csv'):gmatch('[^\r\n]+') do
    if line:sub(1,1)~='#' then
        local id=tonumber(field(line,',',17))
        if id then templates[id]={name=field(line,',',1),display=field(line,',',2),memo=field(line,',',3),
            argument=field(line,',',4),use=field(line,',',11),gra=tonumber(field(line,',',18)),kind=tonumber(field(line,',',20))} end
    end
end
local shop,original,client=env(),env(),env()
client.math=setmetatable({mod=math.fmod},{__index=math}) -- target client uses Lua 5.1
local role,npcindex,npcobject=10,20,30
local balance,bag,notices,queries,packets,saves={},{},{},{},{},0
local baglimit=15
local function reset()
    balance={vip=1000000,crystal=1000000,['石币']=1000000,['族战积分']=1000000,['声望']=100000000,pay=1000000}
    bag={}; notices={}; queries={}; saves=0; baglimit=15
end
reset()
local function instance(i) return bag[i-100] end
local function template(i) return assert(templates[i], 'native template missing: '..tostring(i)) end
shop.item={
    getSecretNameFromNumber=function(id) return template(id).display end,
    getgraNoFromITEMtabl=function(id) return template(id).gra end,
    getItemInfoFromNumber=function(id) return template(id).memo end,
    getCharItemtabl=function(id,k) return assert(template(id)[({['使用函数名']='use',['字段']='argument'})[k]]) end,
    getIntItemtabl=function(id,k) assert(k=='类型'); return template(id).kind end,
    check=function(i) return instance(i) and 1 or 0 end,
    getChar=function(i,k)
        local a=instance(i); if not a then return '' end
        return a[k] or template(a.id)[assert(({['使用函数名']='use',['字段']='argument',['名称']='name',['显示名']='display',['说明']='memo'})[k],k)]
    end,
    getInt=function(i,k)
        local a=assert(instance(i)); if k=='序号' then return a.id end
        if k=='堆叠' then return a.stack or 1 end
        return a[k] or 0
    end,
    setChar=function(i,k,v) assert(instance(i))[k]=v end,
    setInt=function(i,k,v) assert(instance(i))[k]=v end,
    UpdataItemOne=noop,
}
shop.char={
    getWorkInt=function(i,k) assert(i==npcindex and k=='对象'); return npcobject end,
    getInt=function(i,k) assert(i==role); return assert(balance[k],k) end,
    setInt=function(i,k,v) assert(i==role and balance[k]); balance[k]=v end,
    getChar=function(i,k) assert(i==role); return ({['账号']='fixture',['名字']='fixture'})[k] or '' end,
    Additem=function(i,id)
        assert(i==role); template(id)
        if #bag>=baglimit then return -1 end
        bag[#bag+1]={id=id}; return 100+#bag
    end,
    getItemIndex=function(i,slot) assert(i==role); return bag[slot-8] and 100+slot-8 or -1 end,
    newMessageToCli=function(i,_,s) assert(i==role); notices[#notices+1]=s end,
    sendStatusString=noop,
    charSaveFromConnect=function(i) assert(i==role); saves=saves+1 end,
}
shop.other={
    getString=field,
    atoi=function(v) return math.floor(tonumber(v) or 0) end,
    CallFunction=function(name,path,args)
        assert(name=='GetGoldSend' or name=='updateHuoyue' or name=='setCostData' or name=='weixin',name)
        assert(args[1]==role)
    end,
}
shop.sasql={
    getVipPoint=function() return balance.vip end,setVipPoint=function(_,v) balance.vip=v end,
    getPetPoint=function() return balance.crystal end,setPetPoint=function(_,v) balance.crystal=v end,
    getPayPoint=function() return balance.pay end,setPayPoint=function(_,v) balance.pay=v end,
    query=function(q) queries[#queries+1]=q; return 1 end,
}
shop.npc={DelItem=function(i,s)
    assert(i==role); local id,cost=s:match('^(%d+)%*(%d+)$'); id,cost=tonumber(id),tonumber(cost)
    for _,a in ipairs(bag) do if a.id==id and cost>0 then
        local take=math.min(a.stack or 1,cost); a.stack=(a.stack or 1)-take; cost=cost-take
    end end
    assert(cost==0,'native gem debit must be covered')
end}
local function window(i,id,buttons,seq,obj,s)
    assert(i==role and obj==npcobject)
    packets[#packets+1]={id=id,body=s}
end
shop.lssproto={windows=window,windowsupdate=window}
script('runtime/gmsv/data/ablua/npc/shop/vippoint.lua',original); original.data()
script(arg[1] or 'runtime/gmsv/data/ablua/npc/shop/vippoint.lua',shop); shop.data(); shop.npcindex=npcindex
for _,name in ipairs({'itemid','itemid2','itemid3','itemid4','itemid5','itemid6'}) do
    assert(#shop[name]==#original[name],'native catalog row count changed')
    for i,row in ipairs(original[name]) do
        assert(shop[name][i][1]==row[1] and shop[name][i][2]==row[2],'native ID/price/order changed')
    end
end
client.cons={pc={vipGold='vip',gold='石币',dp='族战积分',FameGold='fame',crystalGold='crystal',item='item'},item={useFlag='useFlag'}}
client.char={getPc=function(k) if k=='fame' then return balance['声望']/100 end return balance[k] or 0 end,
    getItem=function(_,slot,k) assert(k=='useFlag'); return bag[slot-8] and 1 or 0 end}
client.sa=setmetatable({getStringToken=function(s,n) return field(s,'|',n) end,
    getIntegerToken=function(s,n) return tonumber(field(s,'|',n)) or 0 end,
    getTextLength=function(s) return #tostring(s) end,getLuaPath=function() return 'client/lua' end}, {__index=function() return noop end})
local rendered={}
client.sp=setmetatable({StockDispBuffer=function(_,_,_,_,_,_,callback,id)
    if callback=='selectItemFunc' then rendered[#rendered+1]=id end
end},{__index=function() return noop end})
client.win={getWinSizeX=function() return 800 end,getWinSizeY=function() return 600 end,
    getIndexWN=function() return npcobject end,getIdWN=function() return 0 end,DelWindow1=function() return 0 end}
local callbacks={}
client.lssproto={windows=function(obj,seq,select,s)
    assert(obj==npcobject and seq==0 and select==0); callbacks[#callbacks+1]=s
    shop.WindowTalked(npcindex,role,seq,select,s)
end}
client.x=388; client.y=300; client.addr=0
client_script('client/lua/win/1017.xy',client)
local function receive(expectedType,list)
    local p=assert(packets[#packets]); assert(p.id==1017,'shop must target 1017; 1034 is spectate')
    client.WindowInit(p.body); client.ServerWindowType()
    assert(client.nowType==expectedType and #client.sealItem==#list)
    for i,row in ipairs(list) do
        local got=client.sealItem[i]; local t=template(row[1])
        assert(got.name==t.display and got.graNo==t.gra and got.price==row[2],'actual client catalog must preserve native row and price')
        assert(got.memo==t.memo and got.fenlei>=1 and got.fenlei<=5,'target five-field row: category then description')
    end
end
shop.ShowList(role); receive(1,shop.itemid)
local currencies={
    {1,'itemid','vip',1},{2,'itemid4','石币',1},{3,'itemid3','族战积分',1},
    {4,'itemid2','声望',100},{5,'itemid','crystal',1},
}
for _,c in ipairs(currencies) do
    reset(); client.selectTypeFunc(c[1]); receive(c[1],shop[c[2]])
    assert(client.pGold==balance[c[3]]/c[4],'target client currency balance must match native debit')
    local row=1; while shop[c[2]][row][2]==0 do row=row+1 end
    local before={}; for k,v in pairs(balance) do before[k]=v end
    client.selectItemFunc(row); client.addItemFunc(2); client.buyWndFunc()
    assert(#bag==2 and bag[1].id==shop[c[2]][row][1],'actual buy callback must grant selected native item')
    for k,v in pairs(before) do
        local cost=k==c[3] and shop[c[2]][row][2]*2*c[4] or 0
        assert(balance[k]==v-cost,'purchase debited the wrong currency: '..k)
    end
    assert(saves>0,'successful purchase must save the native character')
    if c[1]==5 then assert(bag[1]['名称']:sub(1,1)=='*','native crystal purchase binding must remain') end
    -- The server rejects malformed, out of bounds and insufficient funds requests.
    local count=#bag; local money=balance[c[3]]; local saved=saves
    for _,s in ipairs({'B|99|1|1','B|1|0|1|2','B|1|99999|1|2','B|1|1|-1|2','B|1|1|46|2','B|1|1|1|3','B|1|1.5|1|2','B|1|1|1.5|2'}) do
        shop.WindowTalked(npcindex,role,0,0,s)
    end
    assert(#bag==count and balance[c[3]]==money and saves==saved,'invalid requests must not mutate or save')
    for _,quantity in ipairs({30,45}) do
        reset(); baglimit=45
        client.selectTypeFunc(c[1]); receive(c[1],shop[c[2]])
        local bulkBefore=balance[c[3]]
        client.selectItemFunc(row)
        for amount=2,quantity do client.addItemFunc(2) end
        client.buyWndFunc()
        assert(#bag==quantity and saves>0,'actual target bulk quantity must fill all requested pages, not silently return')
        assert(balance[c[3]]==bulkBefore-shop[c[2]][row][2]*quantity*c[4], 'bulk debit must equal actual delivered quantity')
    end
    reset(); balance[c[3]]=0; shop.WindowTalked(npcindex,role,0,0,('B|%d|%d|1|2'):format(c[1],row))
    assert(#bag==0 and balance[c[3]]==0 and saves==0,'insufficient funds must not grant/save')
    reset(); baglimit=1
    shop.WindowTalked(npcindex,role,0,0,('B|%d|%d|2|2'):format(c[1],row))
    assert(#bag==1 and saves>0,'partial capacity purchase must save the one item granted')
    assert(balance[c[3]]==before[c[3]]-shop[c[2]][row][2]*c[4],'full bag must only debit granted items')
    local expectedName=shop.item.getChar(101,c[1]==1 and '名称' or '显示名')
    assert(table.concat(notices,'\n'):find('购买1个 '..expectedName,1,true),
        'partial purchase receipt must name the actual delivered item')
    reset(); baglimit=0
    shop.WindowTalked(npcindex,role,0,0,('B|%d|%d|1|2'):format(c[1],row))
    assert(#bag==0 and saves==0 and #queries==0,'full bag must not record a nonexistent purchase')
    assert(not table.concat(notices,'\n'):find('购买0个',1,true),'full bag must only report failure')
end
reset(); shop.WindowTalked(npcindex,role,0,0,'B|1|1|1|2')
assert(#bag==1 and balance.vip==1000000 and saves>0,'native zero-price item must be granted and saved')
reset(); baglimit=0; shop.WindowTalked(npcindex,role,0,0,'B|1|1|1|2')
assert(#bag==0 and saves==0,'completely full bag must not save a nonexistent purchase')
-- Filter buttons must retain the native catalog row index used by purchase callbacks.
reset(); shop.ShowList(role); receive(1,shop.itemid)
local seen={}
for i,record in ipairs(client.sealItem) do seen[record.fenlei]=(seen[record.fenlei] or 0)+1 end
assert(seen[1] and seen[2] and seen[3] and not seen[4] and not seen[5],
    'native main catalog contains items, pets and equipment; do not invent skins or riding permits')
for category=1,5 do
    rendered={}; client.selectfenlei(category); client.ServerWindowType()
    assert(#rendered==(seen[category] or 0))
    for _,row in ipairs(rendered) do assert(client.sealItem[row].fenlei==category) end
    client.selectfenlei(category)
end
-- The old gem entry has an explicit type 8 and an inventory-backed balance.
reset(); bag={{id=22061,stack=5}}
shop.ShowList5(role); receive(8,shop.itemid5); assert(client.pGold==5 and client.sealItem[1].fenlei==4)
client.selectItemFunc(1); client.buyWndFunc()
assert(#bag==2 and bag[2].id==shop.itemid5[1][1] and bag[1].stack==3 and saves>0)
for _,kind in ipairs({'gem','pay'}) do
    for _,free in ipairs({0,1}) do
        reset()
        local itemSlot=101
        if kind=='gem' then
            bag={{id=22061,stack=5}}; baglimit=1+free; itemSlot=102
            shop.BuyItem5(npcindex,role,1,2)
            assert(bag[1].stack==5-2*free,'partial gem debit must match delivered quantity')
        else
            baglimit=free
            shop.BuyItem6(npcindex,role,1,2)
            assert(balance.pay==1000000-shop.itemid6[1][2]*free,'partial pay debit must match delivered quantity')
        end
        local receipt=table.concat(notices,'\n')
        if free==0 then
            assert(saves==0 and not receipt:find('购买0个',1,true),'empty delivery must not report success')
        else
            assert(saves>0 and receipt:find('购买1个 '..shop.item.getChar(itemSlot,'显示名'),1,true),
                'partial gem/pay receipt must retain the actual delivered name')
        end
    end
end
print('service_menus: target 1017 round-trip, five fields, five currencies, native prices/categories, actual callbacks, validation, partial bag and gem save passed')

-- The directory must lead to the existing dungeon entry and battle implementations.
local dungeons,mowang,hunting,dc=env(),env(),env(),env()
local enabled,faildb,dailyrow,entrywarps,battles,tickets=false,false,nil,{},{},{}
local requests,npcs,nextnpc={}, {}, 1000
local team=0
local function native_environment(e)
    e.os=setmetatable({getenv=function(k) assert(k=='STONEAGE_LOCAL_DUNGEONS'); return enabled and '1' or nil end},{__index=os})
    e.config={getGameservername=function() return 'local-fixture' end}
    e.char={check=function(i) return (i==role or i==role+1 or npcs[i]) and 1 or 0 end,
        getInt=function(i,k) if k=='地图号' then return 2000 end return 0 end,
        getWorkInt=function(i,k)
            if k=='对象' then return i+100 end
            if k=='组队' then return team end
            if k=='战斗' then return 0 end
            if k=='队员1' then return role end
            if k=='队员2' then return role+1 end
            if k:match('^队员') then return -1 end
            return npcs[i] and npcs[i][k] or 0
        end,
        getChar=function(i,k)
            if k=='账号' then return "fixture'account" end
            if k=='名字' then return npcs[i] and npcs[i].name or "角色'\\测试" end
            return ''
        end,
        setWorkInt=function(i,k,v) npcs[i][k]=v end,setInt=noop,setFunctionPointer=noop,
        AllWarpToSpecificPoint=function(i,f,x,y) entrywarps[#entrywarps+1]={i,f,x,y} end,
        TalkToCli=noop,newMessageToCli=noop,ToAroundChar=noop,
    }
    e.npc={CreateNpc=function(name,_,f,x,y)
        nextnpc=nextnpc+1; npcs[nextnpc]={name=name,floor=f,x=x,y=y}; return nextnpc
    end,isFaceToFace=function() return 1 end,
        Free=function(_,i) return (tickets[i] or 0)>0 and 1 or 0 end,
        DelItem=function(i,s) assert(s=='20900*1'); tickets[i]=tickets[i]-1 end,
    }
    e.other={getString=field,atoi=function(s) return tonumber(s) or 0 end,CallFunction=function(name,path,args)
        local target=({['data/ablua/npc/newFB/NewFB.lua']=dungeons,
            ['data/ablua/npc/YamaKing/MoWang.lua']=mowang,['data/ablua/npc/hunting/Hunting.lua']=hunting})[path]
        return assert(target[name],name)(table.unpack(args))
    end}
    e.item=shop.item
    e.battle={CreateVsEnemy=function(i,n,t) battles[#battles+1]={i,n,t} end}
    e.magic={addLUAListFunction=noop}
    e.lssproto={windows=function(i,id,_,seq,obj,s) requests[#requests+1]={i=i,id=id,seq=seq,obj=obj,body=s} end}
    e.lssproto.windowsupdate=e.lssproto.windows
end
for _,e in ipairs({dungeons,mowang,hunting}) do native_environment(e) end
local sql={}
dungeons.sasql={query=function(s)
    sql[#sql+1]=s
    if faildb then return 0 end
    if s:find('insert into NewFB',1,true) then dailyrow=os.date('%m%d')..'/0' end
    return 1
end,free_result=noop,store_result=noop,num_rows=function() return dailyrow and 1 or 0 end,
    fetch_row=noop,data=function() return dailyrow end}
script(arg[2] or 'runtime/gmsv/data/ablua/npc/newFB/NewFB.lua',dungeons)
script(arg[3] or 'runtime/gmsv/data/ablua/npc/YamaKing/MoWang.lua',mowang)
script(arg[4] or 'runtime/gmsv/data/ablua/npc/hunting/Hunting.lua',hunting)
assert(dungeons.ShowAppDungeons,'target dungeon menu 124 has no native directory adapter')
dungeons.main(); mowang.main(); hunting.main()
assert(not mowang.appDungeonNpc and not hunting.appDungeonNpc,'off-server NPCs must remain disabled without explicit local switch')
enabled=true; mowang.main(); hunting.main()
assert(npcs[mowang.appDungeonNpc] and npcs[hunting.appDungeonNpc],'local switch must create the original native NPCs')
dc.sa=client.sa; dc.sp=client.sp; dc.win=setmetatable({CloseWindow=noop},{__index=client.win})
dc.x=400; dc.y=300; dc.addr=0; dc.fuben={{},{},{}}; dc.nowid=1; dc.nowzid=1
dc.lssproto={windows=function(obj,seq,select,s)
    assert(obj==dungeons.appDungeonNpc+100 and seq==1131)
    dungeons.WindowTalked(dungeons.appDungeonNpc,role,seq,select,s)
end}
dc.win.getIndexWN=function() return dungeons.appDungeonNpc+100 end; dc.win.getIdWN=function() return 1131 end
client_script('client/lua/win/1131.xy',dc)
local function dungeon_receive()
    local r=requests[#requests]; assert(r.id==1131 and r.seq==1131)
    dc.WindowInit(r.body)
end
dungeons.ShowAppDungeons(role); dungeon_receive()
assert(dc.fuben[1][1].name=='单人日常副本' and #dc.fuben[1][1].jp>0)
dc.selectjplist(1); dungeon_receive()
assert(dc.fuben[1][1].jp[1].name==template(20810).display,'reward details must use actual native item template')
dc.chuansong()
assert(entrywarps[1][2]==3 and entrywarps[1][3]==12 and entrywarps[1][4]==13,'daily directory must use the original entry')
assert(sql[1]:find("CONVERT(X'",1,true) and sql[2]:find("CONVERT(X'",1,true))
assert(not sql[2]:find("角色'\\测试",1,true),'SQL must encode the complete quoted UTF-8 character name safely')
dailyrow=os.date('%m%d')..'/2'; local warps=#entrywarps
dungeons.AppDungeonEnter(role); assert(#entrywarps==warps,'daily limit must prevent entry')
dailyrow=nil; faildb=true; dungeons.AppDungeonEnter(role); assert(#entrywarps==warps,'failed daily query must not bypass the limit')
faildb=false
dc.selectTypeFunc(2); dungeon_receive(); assert(dc.fuben[2][1].name=='荒原大魔王')
dc.chuansong(); assert(entrywarps[#entrywarps][2]==2005 and #battles==0,'directory must lead to original challenge NPC')
-- Original NPC still owns ticket validation, party debit and battle creation.
team=1; tickets[role]=1; tickets[role+1]=0
mowang.WindowTalked(mowang.appDungeonNpc,role,1,0,'1')
assert(#battles==0 and tickets[role]==1,'one unqualified member must prevent every debit')
tickets[role+1]=1; mowang.WindowTalked(mowang.appDungeonNpc,role,1,0,'1')
assert(#battles==1 and tickets[role]==0 and tickets[role+1]==0)
team=0
dc.selectTypeFunc(3); dungeon_receive(); assert(dc.fuben[3][1].name=='龙域副本')
warps=#entrywarps; dc.chuansong(); assert(#entrywarps==warps,'closed activity must remain closed')
hunting.start=1; hunting.AppDungeonEnter(role)
assert(entrywarps[#entrywarps][2]==12222,'open activity must use its native entry')
warps=#entrywarps
for _,s in ipairs({'G|4|1','G|1|0','G|1|1.5','G|1|999','Q|999999'}) do
    dungeons.WindowTalked(dungeons.appDungeonNpc,role,1131,0,s)
end
assert(#entrywarps==warps,'unknown directory entries must not cause a transfer')
local menu,menu_calls,menu_battle,has_family=env(),{},0,false
menu.char={getWorkInt=function(_,k) assert(k=='战斗'); return menu_battle end,
    getChar=function(_,k) assert(k=='家族'); return has_family and 'Fixture' or '' end}
menu.other={CallFunction=function(name,path,args) assert(args[1]==role); menu_calls[#menu_calls+1]={name,path} end}
script(arg[5] or 'runtime/gmsv/data/ablua/samenu.lua',menu)
local routes={{31,'ShowList','npc/shop/vippoint.lua'},{115,'ShowFamilyList','npc/family/family.lua'},
    {114,'ShowMyFamily','npc/family/family.lua'},{124,'ShowAppDungeons','npc/newFB/NewFB.lua'}}
for _,r in ipairs(routes) do
    has_family=r[1]==114; local before=#menu_calls; menu.FreeSaMenu(role,r[1])
    assert(#menu_calls==before+1 and menu_calls[#menu_calls][1]==r[2]
        and menu_calls[#menu_calls][2]=='data/ablua/'..r[3],'actual 202 dispatcher must reach the native service')
end
menu_battle=1; local before=#menu_calls; menu.FreeSaMenu(role,124)
assert(#menu_calls==before,'native battle restriction must still suppress dungeon entry')
print('service_menus: actual 1131 parser/callbacks, native dungeon entries, UTF-8 SQL literals, daily errors/limits, local switch, party tickets and activity gate passed')
