-- Exercise the real server scripts and the target client's decoded Lua in memory.
-- Target click 0x7b3e0d/0x7b3e15 sends 821/8/""; warpanim.xy sends 821/9/id.
-- 1016.xy SHA256 c8fe25ed34536ad2f750c0a2cebface6b06968c3db4f77e23481814f7218daee.
-- 1033.xy is the target treasure window; its old teleport meaning is incompatible.
local function readfile(path)
    local file = assert(io.open(path, 'rb'))
    local text = file:read('*a')
    file:close()
    return text
end

local function environment()
    local env = setmetatable({}, {__index=_G})
    env._G = env
    return env
end

local function script(path, env)
    assert(load(readfile(path), '@'..path, 't', env))()
end

local function client_script(path, env)
    -- Same target transform as tools/inspect_client_xy.py, with a lossless check.
    local steps = {81,94,66,5,9,35,71,13,26}
    local raw = readfile(path)
    local text, length = raw, #raw
    local function complement(value)
        return (value:gsub('.', function(byte) return string.char(255-byte:byte()) end))
    end
    for i=#steps,1,-1 do
        local cut = length-math.floor(length*steps[i]/100)
        text = complement(text:sub(cut+1)) .. text:sub(1,cut):reverse()
    end
    local roundtrip = text
    for _,percent in ipairs(steps) do
        local count = math.floor(length*percent/100)
        roundtrip = roundtrip:sub(count+1):reverse() .. complement(roundtrip:sub(1,count))
    end
    assert(roundtrip == raw, 'target Lua resource must round-trip without modification')
    assert(load(text, '@'..path, 't', env))()
end

local function token(text, index)
    local current, start = 1, 1
    while true do
        local finish = text:find('|', start, true)
        if current == index then return text:sub(start, finish and finish-1 or #text) end
        if not finish then return '' end
        current, start = current+1, finish+1
    end
end

local server, menu, client, animation = environment(), environment(), environment(), environment()
local leader, npcindex, npcobject, fd = 10, 90, 91, 7
local roles, warps, discharges, notices, menu_calls, packets = {}, {}, {}, {}, {}, {}
for index=10,14 do roles[index]={floor=2000,x=55,y=58,level=1,trans=0,party=index==leader and 1 or 2} end
local process, tick, closed = 9, 0, 0
local noop = function() end
local native_char = {
    check=function(index) return roles[index] and 1 or 0 end,
    getInt=function(index, field)
        local role = assert(roles[index], 'fixture character must exist')
        return assert(role[({['地图号']='floor',['转数']='trans',['等级']='level'})[field]])
    end,
    getWorkInt=function(index, field)
        if index==npcindex and field=='对象' then return npcobject end
        assert(roles[index])
        if field=='组队' then return roles[index].party end
        local member = field:match('^队员([1-5])$')
        if member then return leader+tonumber(member)-1 end
        error('unexpected character work field: '..field)
    end,
    getWorkChar=function(index, field)
        assert(roles[index] and field=='MAC2')
        return 'app4-windows'
    end,
    newMessageToCli=function(index, _, text) notices[#notices+1]={index,text} end,
    DischargeParty=function(index, flag)
        assert(flag==1); discharges[#discharges+1]=index; roles[index].party=0
    end,
    WarpToSpecificPoint=function(index, floor, x, y)
        assert(roles[index]); warps[#warps+1]={index,floor,x,y}
        roles[index].floor, roles[index].x, roles[index].y = floor,x,y
    end,
}
local other = {
    atoi=function(value) return tonumber(value) or 0 end,
    getString=function(value, delimiter, index) assert(delimiter=='|'); return token(value,index) end,
    CallFunction=function(name, path, args)
        assert(path=='data/ablua/npc/chuansong/chuansong.lua')
        return assert(server[name])(table.unpack(args))
    end,
}
server.char, menu.char, server.other, menu.other = native_char,native_char,other,other
server.config = {getGameservername=function() return 'local-test-line' end}
menu.net = {getCharaindex=function(socket) assert(socket==fd); return leader end}

local cons = {config={ProcNo=1,nowGx=2,nowGy=3,OpenFixedWindowsFlag=4},pc={status=1,ptAct=2}}
local client_char = {
    setConfig=noop,
    getConfig=function(key)
        if key==cons.config.ProcNo then return process end
        if key==cons.config.nowGx then return roles[leader].x end
        if key==cons.config.nowGy then return roles[leader].y end
        error('unexpected client configuration field')
    end,
    getPc=function() return 0 end,
}
local client_sa = {
    getStringToken=token,
    getIntegerToken=function(text,index) return tonumber(token(text,index)) or 0 end,
    getLuaPath=function() return 'client/lua' end,
    getServerGold=noop, closeWnd=function() closed=closed+1 end,
    PlaySe=noop, StockPromptBufferLine=noop, StockDispBuffer=noop,
    getTickCount=function() return tick end, delCharmManor=noop, delCharmBody=noop,
    CallFunction=function(name, path, args)
        assert(name=='init' and path=='client/lua/warpanim.lua')
        return animation.init(table.unpack(args))
    end,
}
local client_win = {
    getWinSizeX=function() return 1280 end, getWinSizeY=function() return 720 end,
    getIndexWN=function() return npcobject end, getIdWN=function() return 0 end,
    CloseWindow=noop,
}
local scrolling = {CreateScrollPanel=noop,SetScrollMode=noop,SetScrollY=noop,SetScrollX=noop,
                   SetScrollDir=noop,DetachEvent=noop,AttachEvent=noop,StockDispBuffer=noop,SetFixedPanel=noop}
local client_lssproto = {
    windows=function(object, sequence, select, text)
        assert(object==npcobject and sequence==0 and select==0)
        server.WindowTalked(npcindex,leader,sequence,select,text)
    end,
    menuSend=function(index, text, callback)
        assert(callback==''); menu_calls[#menu_calls+1]={index,tostring(text)}
        menu.FreeNewSaMenu(fd,index,tostring(text))
    end,
}
for _,env in ipairs({client,animation}) do
    env.char, env.sa, env.win, env.sp, env.lssproto = client_char,client_sa,client_win,scrolling,client_lssproto
    env.package={path=''}
    env.require=function(name) assert(name=='cons'); return cons end
end
server.lssproto = {}
local function window_packet(kind, index, windowtype, buttons, sequence, object, text)
    assert(index==leader and buttons==0 and sequence==0 and object==npcobject)
    assert(windowtype==1016, '20260805 teleport must use window 1016; 1033 is treasure')
    packets[#packets+1]={kind,text}
    -- Both native opening and updating call WindowInit; WindowInit1 is a test helper.
    client.WindowInit(text)
end
server.lssproto.windows=function(...) return window_packet('open',...) end
server.lssproto.windowsupdate=function(...) return window_packet('update',...) end

script('runtime/gmsv/data/ablua/samenu.lua', menu)
script('runtime/gmsv/data/ablua/npc/chuansong/chuansong.lua', server)
server.data(); server.npcindex=npcindex
client_script('client/lua/win/1016.xy', client); client.data()
client_script('client/lua/warpanim.xy', animation); animation.data()

menu.FreeNewSaMenu(fd,8,'')
assert(#packets==2 and packets[1][1]=='open' and packets[2][1]=='update')
assert(#client.list==#server.mapnamedata and client.list[2].name=='村庄传送')
assert(#client.info==#server.mapdata[1] and client.info[1].warpNo==101)
assert(client.info[1].limitTrn==0 and client.info[1].limitLv==1, 'I rows must carry four fields')
print('PASS: actual 821/8 handler -> 1016 L parser -> H|1| -> 1016 I parser')

client.selectTypeFunc(2,0)
assert(#client.info==#server.mapdata[2] and client.info[1].warpNo==110)
assert(client.info[1].limitTrn==0 and client.info[1].limitLv==10)
local airport
for i,entry in ipairs(client.info) do if entry.warpNo==1016 then airport=i end end
assert(airport and client.info[airport].limitLv==0 and client.info[airport].limitTrn==0)
client.isWarpFunction(airport); client.noWarpFunction()
assert(not client.iswarpFlag and #menu_calls==0 and #warps==0, 'cancel must not request a warp')
client.selectInfoFunc(airport,0)
assert(animation.warpFlag and #menu_calls==0 and #warps==0, 'animation alone is not a server warp')
for i=1,96 do tick=tick+11; animation.proc() end
assert(#menu_calls==1 and menu_calls[1][1]==9 and menu_calls[1][2]=='1016')
assert(#warps==5 and #discharges==0, 'original warp must move all five eligible members')
for i=1,5 do
    assert(warps[i][1]==leader+i-1 and warps[i][2]==5543 and warps[i][3]==24 and warps[i][4]==24)
end
print('PASS: actual client confirmation/animation -> 821/9 -> five native WarpToSpecificPoint calls')

warps, discharges = {}, {}
menu.FreeNewSaMenu(fd,9,'110')
assert(#warps==0 and #notices>0, 'native minimum village level must remain enforced')
for _,role in pairs(roles) do role.level=15 end
roles[12].level=1
menu.FreeNewSaMenu(fd,9,'110')
assert(#warps==4 and #discharges==1 and discharges[1]==12, 'ineligible member must retain native discharge behavior')
warps, discharges = {}, {}
roles[leader].floor=117
menu.FreeNewSaMenu(fd,9,'1016')
assert(#warps==0, 'native blocked map must remain blocked')
roles[leader].floor=2000
menu.FreeNewSaMenu(fd,9,'108')
assert(#warps==0, 'native reincarnation gate must remain enforced')
menu.FreeNewSaMenu(fd,9,'1002')
assert(#warps==0, 'native server-line gate must remain enforced')
for _,value in ipairs({'','bad','999999'}) do menu.FreeNewSaMenu(fd,9,value) end
assert(#warps==0, 'invalid destination must not warp')
local previous = #packets
for _,value in ipairs({'','H|0|','H|999999|','H|bad|','X|1|'}) do
    server.WindowTalked(npcindex,leader,0,0,value)
end
assert(#packets==previous, 'invalid category must not emit a new list')
process=10
local previous_menus = #menu_calls
client.selectInfoFunc(airport,0)
assert(#menu_calls==previous_menus and not animation.warpFlag, 'battle confirmation must not start a warp')
print('PASS: cancellation, invalid input, level/trans/map/line gates and team eligibility preserved')
