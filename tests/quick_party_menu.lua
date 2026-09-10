-- Execute the original AutoTeam/samenu scripts against the actual target Lua UI.
-- Target 821/5 still opens quick party; 1030 is now the skill shop, 1014 is quick party.
-- Original 1014.xy SHA256: 0e54a2c4dd36081b4e986896aa04ecf397e15a02b5534096c5387dae6d43d5d5.
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
    -- Lossless target resource decoding; no substituted parser or UI implementation.
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

local server, menu, client = environment(), environment(), environment()
local applicant, leader, npcindex, npcobject, fd = 10,20,90,91,7
local roles, warps, joins, notices, packets, outgoing = {}, {}, {}, {}, {}, {}
local closed, input, join_result = 0,'',1
local noop = function() end
local function role(index, fields)
    local value = {name='fixture'..index, floor=10001, x=40, y=50, level=15,
                   face=0x020104, party=0, open=1, enhanced=0, members={}}
    for key, item in pairs(fields or {}) do value[key]=item end
    roles[index]=value
    return value
end
role(applicant,{floor=5543,level=1})
role(leader,{name='队长甲',party=1,members={20,21}})
role(21,{party=2})
role(30,{party=1,open=0,members={30}})
role(40,{party=1,members={40,41,42,43,44}})
for index=41,44 do role(index,{party=2}) end
role(50,{party=1,enhanced=1,members={50}})
role(60,{party=1,floor=5543,members={60}})
role(70,{name='队长乙',party=1,floor=31401,level=30,members={70}})

local native_char = {
    check=function(index) return roles[index] and 1 or 0 end,
    getPlayerMaxNum=function() return 100 end,
    getInt=function(index, field)
        local entry = assert(roles[index])
        local key = assert(({['地图号']='floor',['坐标X']='x',['坐标Y']='y',
                            ['等级']='level',['头像号']='face',['组队增强模式']='enhanced'})[field])
        return assert(entry[key])
    end,
    getChar=function(index, field) assert(field=='名字'); return assert(roles[index]).name end,
    getFlg=function(index, field) assert(field=='组队'); return assert(roles[index]).open end,
    getWorkInt=function(index, field)
        if index==npcindex and field=='对象' then return npcobject end
        local entry = assert(roles[index])
        if field=='组队' then return entry.party end
        local member = field:match('^队员([1-5])$')
        if member then return entry.members[tonumber(member)] or -1 end
        error('unexpected character work field: '..field)
    end,
    newMessageToCli=function(index, _, text) notices[#notices+1]={index,text} end,
    WarpToSpecificPoint=function(index, floor, x, y)
        assert(index==applicant); warps[#warps+1]={index,floor,x,y}
        roles[index].floor,roles[index].x,roles[index].y=floor,x,y
    end,
    JoinParty=function(target, member)
        assert(roles[target] and member==applicant)
        joins[#joins+1]={target,member}
        if join_result==1 then
            roles[member].party=2
            table.insert(roles[target].members,member)
        end
        return join_result
    end,
}
local other = {
    atoi=function(value) return tonumber(value) or 0 end,
    getString=function(value, delimiter, index) assert(delimiter=='|'); return token(value,index) end,
    NumAndNum=function(a,b) return a & b end,
    Random=function(low, high) assert(low==1 and high==100000); return 1 end,
    CallFunction=function(name, path, args)
        assert(name=='ShowHead' and path=='data/ablua/npc/AutoTeam/AutoTeam.lua')
        return server[name](table.unpack(args))
    end,
}
server.char,menu.char,server.other,menu.other=native_char,native_char,other,other
menu.net={getCharaindex=function(socket) assert(socket==fd); return applicant end}
client.sa={
    getStringToken=token,
    getIntegerToken=function(text,index) return tonumber(token(text,index)) or 0 end,
    getLuaPath=function() return 'client/lua' end,
    getServerGold=noop, PlaySe=noop,
    closeWnd=function() closed=closed+1 end,
    StockPromptBufferLine=function(text) notices[#notices+1]={applicant,text} end,
}
client.win={
    getWinSizeX=function() return 1280 end, getWinSizeY=function() return 720 end,
    getIndexWN=function() return npcobject end, getIdWN=function() return 0 end,
    CloseWindow=noop, GetKeyInputFocus=noop, InitStrBuffer=noop,
    GetInputMsg=function(index) assert(index==0); return input end,
}
client.sp={SetScrollY=noop}
client.package={path=''}
client.require=function(name) assert(name=='cons'); return {} end
client.lssproto={windows=function(object,sequence,select,text)
    assert(object==npcobject and sequence==0 and select==0)
    outgoing[#outgoing+1]=text
end}
local function deliver(expected)
    assert(#outgoing==1 and outgoing[1]==expected, 'actual target callback payload differs')
    local text=table.remove(outgoing,1)
    -- Deliver after the UI callback returns, as a real asynchronous server reply does.
    server.WindowTalked(npcindex,applicant,0,0,text)
end
local function window_packet(kind,index,windowtype,buttons,sequence,object,text)
    assert(index==applicant and buttons==8 and sequence==0 and object==npcobject)
    assert(windowtype==1014, '20260805 quick party must use window 1014; 1030 is the skill shop')
    packets[#packets+1]={kind,text}
    client.WindowInit(text)
end
server.lssproto={
    windows=function(...) return window_packet('open',...) end,
    windowsupdate=function(...) return window_packet('update',...) end,
}

script('runtime/gmsv/data/ablua/samenu.lua',menu)
script('runtime/gmsv/data/ablua/npc/AutoTeam/AutoTeam.lua',server)
server.data(); server.npcindex=npcindex; server.teamdata={}
client_script('client/lua/win/1014.xy',client); client.data()

menu.FreeNewSaMenu(fd,5,'')
assert(#packets==1 and packets[1][1]=='open' and #outgoing==0)
assert(#client.list==7 and client.list[1]=='[10-25]级' and #client.listChar==0)
client.selectTypeFunc(1,0); deliver('G|1|')
assert(#client.listChar==1, 'closed/full/enhanced/wrong-map leaders must be excluded')
local entry=client.listChar[1]
assert(entry.name=='队长甲' and entry.graNo==30022 and entry.mouthNo==30029 and entry.eyeNo==30027)
assert(entry.lv==15 and entry.num==2 and entry.index=='20', 'all seven native I row fields must agree')
assert(#warps==0 and #joins==0, 'opening or querying must not join a party')
print('PASS: actual 821/5 -> 1014 L/G/I parsing, seven-field faces/level/count/index and native filters')

client.selectTeamFunc(1,0); assert(client.teamWndFlag)
client.cancelFunc(0,0)
assert(not client.teamWndFlag and #outgoing==0 and #warps==0 and #joins==0)
client.findTeamFunc(0,0); assert(client.findWndFlag)
client.findTeamSubMit(0,0); assert(#outgoing==0 and client.findWndFlag)
input='队长甲'; client.findTeamSubMit(0,0); deliver('F|1|队长甲|')
assert(not client.sendFlag and #client.listChar==1 and client.listChar[1].name==input)
client.refreshFunc(0,0); deliver('G|1|')
client.selectTypeFunc(2,0); deliver('G|2|')
assert(#client.listChar==1 and client.listChar[1].name=='队长乙' and client.listChar[1].lv==30)
client.selectTypeFunc(3,0); deliver('G|3|')
assert(#client.listChar==0 and client.selectTypeIndex==3, 'empty result is a valid native I response')
client.selectTypeFunc(1,0); deliver('G|1|')
print('PASS: actual target cancellation, empty search guard, Chinese exact-name F, refresh and category changes')

client.selectTeamFunc(1,0); client.submitFunc(0,0); deliver('J|1|20|')
assert(#warps==0 and #joins==0 and notices[#notices][2]:find('等级不足10',1,true))
roles[applicant].level=10
menu.FreeNewSaMenu(fd,5,''); client.selectTypeFunc(1,0); deliver('G|1|')
client.selectTeamFunc(1,0); client.submitFunc(0,0); deliver('J|1|20|')
assert(#warps==1 and #joins==1 and joins[1][1]==leader and joins[1][2]==applicant)
assert(warps[1][2]==10001 and math.abs(warps[1][3]-40)<=1 and math.abs(warps[1][4]-50)<=1)
assert(roles[applicant].party==2 and notices[#notices][2]=='快捷组队成功' and closed==2)
local previous=#packets
menu.FreeNewSaMenu(fd,5,''); server.WindowTalked(npcindex,applicant,0,0,'J|1|20|')
assert(#packets==previous and #warps==1 and #joins==1, 'already joined applicant must not rejoin or reopen')
print('PASS: native minimum level, actual target J confirmation, native warp/join invocation and duplicate guard')

roles[applicant].party=0; roles[leader].members={20,21}
menu.FreeNewSaMenu(fd,5,''); client.selectTypeFunc(1,0); deliver('G|1|')
roles[leader].members={20,21,41,42,43}
client.selectTeamFunc(1,0); client.submitFunc(0,0); deliver('J|1|20|')
assert(#warps==1 and #joins==1 and notices[#notices][2]=='该队伍不存在或已满员', 'stale list must be revalidated')
roles[leader].members={20,21}; join_result=0
menu.FreeNewSaMenu(fd,5,''); client.selectTypeFunc(1,0); deliver('G|1|')
client.selectTeamFunc(1,0); client.submitFunc(0,0); deliver('J|1|20|')
assert(#warps==2 and #joins==2 and roles[applicant].party==0 and notices[#notices][2]=='快捷组队失败')
previous=#packets
for _,value in ipairs({'','G|0|','G|999|','G|bad|','J|0|20|','J|999|20|','J|1||','J|1|999|','X|1|'}) do
    server.WindowTalked(npcindex,applicant,0,0,value)
end
assert(#packets==previous and #warps==2 and #joins==2)
print('PASS: stale/full team recheck, native join failure is not reported as success, invalid G/J requests')
