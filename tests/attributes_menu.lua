-- Real native resource scripts + target XY; API boundaries only are fixtures.
-- build/lua_reference tests/attributes_menu.lua [staged ablua directory]
local function read(p) local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
local function env() local e=setmetatable({},{__index=_G});e._G=e;return e end
local function script(p,e) assert(load(read(p),'@'..p,'t',e))() end
local function split(s)
    local a={};for v in (s..'|'):gmatch('(.-)|')do a[#a+1]=v end;return a
end
local function xy(p,e)
    local raw=read(p);local s=raw;local steps={81,94,66,5,9,35,71,13,26}
    local function inv(v)return(v:gsub('.',function(c)return string.char(255-c:byte())end))end
    for i=#steps,1,-1 do local cut=#s-math.floor(#s*steps[i]/100);s=inv(s:sub(cut+1))..s:sub(1,cut):reverse()end
    assert(utf8.len(s));local r=s
    for _,p in ipairs(steps)do local cut=math.floor(#r*p/100);r=r:sub(cut+1):reverse()..inv(r:sub(1,cut))end
    assert(r==raw,'target XY byte round trip');assert(load(s,'@'..p,'t',e))()
end
local root=arg[1] or 'runtime/gmsv/data/ablua'
local profile=4;local role=1;local noop=function()end
local ints={['称号状态1']=1,['称号状态2']=(1<<9)|(1<<30),['称号状态3']=0,
    ['文字称号']=0,['称号类型']=0,['图像号']=100010,['原图像号']=100010,['骑宠']=-1,['骑宠名字模式']=0}
local titlepackets,skinpackets={},{}
local deleted=0;local argument='8';local clock=1000
local char={check=function(i)return(i==1 or i==2)and 1 or 0 end,getFd=function()return 7 end,
    getInt=function(i,k)return ints[k]or 0 end,setInt=function(i,k,v)ints[k]=v end,
    getChar=function(i,k)assert(k=='账号');return'fixture'end,
    getCharPet=function(i,p)return p==0 and 2 or -1 end,
    TalkToCli=noop,newMessageToCli=noop,sendStatusString=noop,ToAroundChar=noop,
    DelItem=function()deleted=deleted+1 end}
local other={DataAndData=function(a,b)return a & (1<<b)end,DataOrData=function(a,b)return a|(1<<b)end,
    DataNotData=function(a,b)return a & ~(1<<b)end,
    getString=function(s,d,n)assert(d=='|');return split(s)[n]or''end,
    atoi=function(v)return math.floor(tonumber(v)or 0)end,time=function()return clock end,
    CallFunction=function(name,path,args)
        if name=='TitleSend'then titlepackets[#titlepackets+1]=args[2]
        else assert(name=='CheckPetRide');return 100010 end
    end}
local title=env();title.char=char;title.other=other;title.net={getloginmark=function()return profile end}
title.item={getChar=function()return argument end,addLUAListFunction=noop}
script(root..'/chartitle.lua',title);title.data()
title.TitleListSend(role)
local rows=split(titlepackets[1])
assert(#rows==2+3*7,'target title list requires seven fields per owned title')
assert(titlepackets[1]=='S|3|0|32566|0|0|1|0|0|1|32607|0|0|42|0|0|1|0|0|0|63|0|0')
local clienttitle=env();xy('client/lua/title.xy',clienttitle);clienttitle.data()
for id=1,64 do
    local r=split(title.AppTitleRow(role,id))
    local originalMapping=clienttitle.NativeOriginalTitleStr or clienttitle.GetTitleStr
    assert(#r==7 and tonumber(r[2])==tonumber(originalMapping(id)))
    if id<=41 then
        assert(clienttitle.GetTitleStr(id)=='[style c=4]'..title.titleinfo[id][1]..'[/style]')
    end
end
title.othertitleuse(role,7);assert(titlepackets[#titlepackets]=='A|0|32572|0|0|7|0|0')
title.titleuse(0,role,role,9);assert(deleted==1 and titlepackets[#titlepackets]=='A|0|32573|0|0|8|0|0')
title.titleuse(0,role,role,9);assert(deleted==1,'duplicate title item must not be consumed')
assert(title.playertitleuse(role,2)==0 and ints['文字称号']==0,'unowned title rejected')
assert(title.playertitleuse(role,7)==1 and ints['文字称号']==7 and titlepackets[#titlepackets]=='N|0|7')
title.playertitleuse(role,0);assert(titlepackets[#titlepackets]=='N|0|0')
profile=3;assert(title.AppTitleRow(role,42)=='1|42|');profile=4

local state={active=0,flg1=(1<<20)|(1<<22),flg2=0,expiry={[23]=2000}}
local skin=env();skin.char=char;skin.other=other;skin.net=title.net;skin.item=title.item
skin.lssproto={S=function(i,s)assert(i==role);skinpackets[#skinpackets+1]=s end}
skin.sasql={
    query=function(q)
        if q:match('^select ') then
            local columns=q:match('^select (.-) from `skin`')
            if columns=='*' then
                -- Migrated physical schema: cdkey/index/flg1/flg2/flg3/time1..96.
                state.expiry_offset=5
            else
                local expected={'`cdkey`','`index`','`flg1`','`flg2`'}
                for i=1,64 do expected[#expected+1]='`time'..i..'`' end
                assert(columns==table.concat(expected,','),'explicit native 64-skin projection')
                state.expiry_offset=4
            end
            return 1
        end
        assert(q:match('^update `skin`'),'unexpected native skin SQL: '..q)
        local active=q:match('`index`=(%d+)');if active then state.active=tonumber(active)end
        local f1=q:match('`flg1`=(%d+)');if f1 then state.flg1=tonumber(f1)end
        local f2=q:match('`flg2`=(%d+)');if f2 then state.flg2=tonumber(f2)end
        for id,value in q:gmatch('`time(%d+)`=(%d+)')do state.expiry[tonumber(id)]=tonumber(value)end
        return 1
    end,free_result=noop,store_result=noop,num_rows=function()return 1 end,fetch_row=noop,
    data=function(n)
        if n==2 then return state.active elseif n==3 then return state.flg1 elseif n==4 then return state.flg2 end
        return state.expiry[n-state.expiry_offset]or 0
    end}
script(root..'/item/skin.lua',skin);skin.data();skin.skinlogin(role)
assert(skinpackets[1]=='Q|I|2|黑帮教父A|108025|0|1|0|0|黑帮教父C|108027|2000|2|0|0')
assert(skinpackets[2]=='Q|A|0')
skin.userskin(role,2);assert(ints['图像号']==100010,'native mount prerequisite preserved')
ints['骑宠']=0;skin.userskin(role,2);assert(ints['图像号']==108027 and state.active==23)
skin.userskin(role,3);assert(ints['图像号']==108027,'unowned skin ordinal rejected')
argument='22|0';skin.skin(0,role,role,9)
assert(deleted==2 and state.flg1==((1<<20)|(1<<21)|(1<<22)))
local full=skinpackets[#skinpackets-1];assert(full:sub(1,6)=='Q|I|3|')
local fields=split(full);assert(#fields==3+3*6 and fields[19]=='3','new skin must rebuild sorted owned ordinals')
assert(skinpackets[#skinpackets]=='Q|A|3','active owned ordinal must follow insertion')
skin.userskin(role,2);assert(state.active==22 and ints['图像号']==108026)
skin.userskin(role,3);assert(state.active==23)
clock=3000;skin.skinlogin(role)
assert(state.flg1==((1<<20)|(1<<21)) and state.active==0,'native expiry and ownership checks run')
assert(skinpackets[#skinpackets]=='Q|A|0','expired active skin must not leave a selected UI ordinal')
-- Already-owned unavailable content is retained, cannot be equipped, and does
-- not consume another skin item. Login restores the real mount, not Uli.
state.flg1=1;state.expiry={};state.active=1;skinpackets={};ints['图像号']=105073
skin.skinlogin(role)
assert(state.flg1==1 and state.active==0 and ints['图像号']==100010)
assert(skinpackets[1]=='Q|I|1|光之刃剑（暂不可用）|0|0|1|0|0')
assert(skinpackets[2]=='Q|A|0')
skin.userskin(role,1);assert(state.active==0 and ints['图像号']==100010)
argument='3|0';skin.skin(0,role,role,9);assert(deleted==2 and state.flg1==1)
assert(skin.rideskin(role)==0)
-- Entitlement slot 34 stays slot 34 in flg2 and activates the verified rider.
state.flg1=0;state.flg2=2;skinpackets={};skin.skinlogin(role)
assert(skinpackets[1]=='Q|I|1|佩露夏-小豆丁|109280|0|1|0|0')
skin.userskin(role,1);assert(state.active==34 and ints['图像号']==109280)
profile=3;state.active=0;state.flg1=3;state.flg2=0;skinpackets={};skin.skinlogin(role)
assert(skinpackets[1]=='Q|I|2|光之刃剑|105073|0|1|魔之刃剑|105074|0|2')
argument='4|0';skin.skin(0,role,role,9)
assert(skinpackets[#skinpackets]=='Q|H|1|透暴赛亚人皮肤|132052|0|4','legacy frame must remain byte compatible')

-- Execute the actual target 1119 parser against the C adapter's emitted frame.
local client=env();client.sa={getStringToken=function(s,n)return split(s)[n]or''end,
    getIntegerToken=function(s,n)return tonumber(split(s)[n])or 0 end}
xy('client/lua/win/1119.xy',client)
local output=os.getenv('STONE_CHECK_OUTPUT_DIR') or 'evidence'
local cframe=read(output..'/attributes-window-fixture.txt'):gsub('[\r\n]+$','')
client.WindowInit(cframe)
assert(client.pays[1]==4000 and client.pays[5]==10000)
assert(client.tianfu[1].used==1 and client.tianfu[1].tl==1000 and client.tianfu[1].point==2000)
assert(client.tianfu[5].used==1 and client.tianfu[5].point==0)
-- The C purchase callback uses this real native branch rather than P.GOLD
-- (stone money) or the older five-integer C currency serializer.
local money=env();local money_values={};local money_function
money.char={check=function()return 1 end,getFd=function()return 7 end,
    getInt=function(i,k)return assert(({['石币']=6666,['声望']=12300,['活力']=40})[k])end,
    getChar=function(i,k)assert(k=='活力时间');return''end}
money.net={getCharaindex=function()return 1 end}
money.sasql={getVipPoint=function()return 9000 end,getPetPoint=function()return 8 end,getPayPoint=function()return 9 end}
money.lssproto={mkbuffer=function(fd)assert(fd==7);money_values={}end,
    mkint=function(fd,v)assert(fd==7);money_values[#money_values+1]=v;return v end,
    SendMesg=function(fd,f)assert(fd==7);money_function=f end}
script('runtime/gmsv/data/ablua/dispatchmessage.lua',money)
assert(money.FreeDispatchMessage(7,261)==1 and money_function==262)
assert(table.concat(money_values,',')=='6666,9000,123,40,0,8,9,15846')
print('attributes_menu.lua: native title/skin ownership, row formats, acquisition ordering, expiry, legacy frames and target 1119 parser passed')
