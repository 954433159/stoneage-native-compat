-- Target 20260805 activity window. Embedded in samenu.lua by the resource recipe.
-- Public external observations: evidence/events-quests-20260910/external-activity*.json.
-- The native character stores this ledger together with its inventory, not in a
-- separate SQL claim table. Old/unknown ledger formats are never overwritten.
local eventSessions={}
local dailyRewards={{600,31000},{1800,31001},{3600,31002},{7200,31002}}
local levelRewards={{0,120,31003,4},{1,120,0,1},{2,120,0,1},{4,120,0,1},{5,120,0,1}}
local function eventNotice(i,text)char.TalkToCli(i,-1,text,4)end
local function eventBit(mask,n)return math.floor(mask/2^(n-1))%2 end
local function eventNumber(s,max)
    if type(s)~='string' or not s:match('^%d+$') then return end
    local n=tonumber(s);if n and n<=max then return n end
end
local function eventState(i)
    local raw=char.getChar(i,'活动领奖')
    local today=tonumber(os.date('%Y%m%d',os.time()))
    if raw=='' then return {day=today,daily=0,level=0} end
    local d,a,b=raw:match('^1|(%d+)|(%d+)|(%d+)$')
    d=eventNumber(d,99991231);a=eventNumber(a,15);b=eventNumber(b,31)
    if not d or not a or not b then eventNotice(i,'领奖记录格式异常，奖励资格保留，请联系本地维护。');return end
    return {day=today,daily=d==today and a or 0,level=b}
end
local function eventStore(i,state)
    local raw=string.format('1|%d|%d|%d',state.day,state.daily,state.level)
    char.setChar(i,'活动领奖',raw)
    return char.getChar(i,'活动领奖')==raw
end
local function eventOnline(i)
    if os.date('%Y%m%d',char.getWorkInt(i,'登陆时间'))~=os.date('%Y%m%d',os.time()) then return 0 end
    return math.max(0,math.min(86400,char.getInt(i,'签到在线时间')*60))
end
local function eventBusy(i)
    return char.check(i)~=1 or char.getWorkInt(i,'战斗')~=0 or char.getWorkInt(i,'交易状态')~=0
        or char.getWorkInt(i,'摆摊')~=-1 or char.getWorkInt(i,'离线')~=0 or char.getFlg(i,'死亡')~=0
end
local function eventSend(i,payload,update)
    if not appEventsNpc or char.check(appEventsNpc)~=1 then return end
    local fn=update and lssproto.windowsupdate or lssproto.windows
    fn(i,1100,0,1100,char.getWorkInt(appEventsNpc,'对象'),payload)
end
local function eventRewardRow(id,count,status)
    if id==0 then return status..'|奖励待匹配|56488|'..count..'|耐久度：无限|资格保留，待完成原生物品匹配。' end
    return status..'|'..item.getSecretNameFromNumber(id)..'|'..item.getgraNoFromITEMtabl(id)..'|'..count
        ..'|耐久度：无限|'..item.getItemInfoFromNumber(id):gsub('|',' ')
end
local function eventDetails(i,category)
    local state=eventState(i);if not state then return end
    local s='X|'..category..'|L|'
    if category==2 then
        s=s..eventOnline(i)
        for n,row in ipairs(dailyRewards)do s=s..'|'..eventRewardRow(row[2],1,eventBit(state.daily,n)) end
    elseif category==3 then
        s=s..'0|'..char.getInt(i,'转数')..'|'..char.getInt(i,'等级')
        for n,row in ipairs(levelRewards)do s=s..'|'..eventRewardRow(row[3],row[4],eventBit(state.level,n)) end
    else return end
    eventSend(i,s,true)
end
function AppEventsOpen(i)
    if char.check(i)~=1 or not eventState(i) then return end
    eventSessions[i]={account=char.getChar(i,'账号'),name=char.getChar(i,'名字'),untilTime=os.time()+600}
    eventSend(i,'L|2|每日福利|2|2|等级达成|3|3|2',false)
end
local function eventGrant(i,id,count,state,key,index)
    local slots={}
    for slot=9,53 do if char.getItemIndex(i,slot)<0 then slots[#slots+1]=slot end end
    if #slots<count then eventNotice(i,'背包空间不足，未领取奖励。');return false end
    local made={}
    for n=1,count do
        local obj=char.Additem(i,id)
        if not obj or item.check(obj)~=1 then
            for _,m in ipairs(made)do
                for slot=9,53 do if char.getItemIndex(i,slot)==m then char.DelItem(i,slot);break end end
            end
            eventNotice(i,'奖励创建失败，领取资格保留。');return false
        end
        made[#made+1]=obj
    end
    local previous=char.getChar(i,'活动领奖')
    state[key]=state[key]+2^(index-1)
    if not eventStore(i,state) then
        for _,m in ipairs(made)do for slot=9,53 do if char.getItemIndex(i,slot)==m then char.DelItem(i,slot);break end end end
        char.setChar(i,'活动领奖',previous);eventNotice(i,'领奖记录写入失败，资格保留。');return false
    end
    char.charSaveFromConnect(i)
    eventNotice(i,'成功领取'..item.getSecretNameFromNumber(id)..' ×'..count..'。')
    return true
end
function AppEventsWindow(me,i,seq,select,data)
    if me~=appEventsNpc or seq~=1100 or type(data)~='string' or #data>48 then return end
    local session=eventSessions[i]
    if char.check(i)~=1 or not session or session.untilTime<os.time()
        or session.account~=char.getChar(i,'账号') or session.name~=char.getChar(i,'名字') then return end
    if select==8 or select==2 and data=='' then eventSessions[i]=nil;return end
    local raw=data:match('^G|(%d+)|$');local index=eventNumber(raw,100)
    if not index or index<1 then return end
    if select==0 then return eventDetails(i,index) end
    if eventBusy(i) then eventNotice(i,'请在战斗、交易或摆摊结束后领取。');return end
    local state=eventState(i);if not state then return end
    local key,row,id,count
    if select==2 then
        row=dailyRewards[index];if not row then return end
        key='daily';id=row[2];count=1
        if eventOnline(i)<row[1] then eventNotice(i,'今日在线时间不足，暂不能领取。');return end
    elseif select==3 then
        row=levelRewards[index];if not row then return end
        key='level';id=row[3];count=row[4]
        local rebirth,level=char.getInt(i,'转数'),char.getInt(i,'等级')
        if rebirth<row[1] or rebirth==row[1] and level<row[2] then eventNotice(i,'转生或等级不足，暂不能领取。');return end
        if id==0 then eventNotice(i,'该档奖励正在匹配原生装备，领取资格保留。');return end
    else return end
    if eventBit(state[key],index)==1 then eventNotice(i,'本档奖励已经领取。');return eventDetails(i,select) end
    eventGrant(i,id,count,state,key,index);eventDetails(i,select)
end
function AppEventsCreate()
    appEventsNpc=npc.CreateNpc('活动福利',100000,777,24,21,4)
    char.setFunctionPointer(appEventsNpc,'窗口事件','AppEventsWindow','')
end
