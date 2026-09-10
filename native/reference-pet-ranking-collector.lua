-- Optional reference-client reader. Original G -> P -> S ranking flow only.
-- Names/UIDs of ranked players and account data are never written to evidence.
local init,draw,choose,choosePlayer=WindowInit,ServerWindowType,selectPet,selectPlayer
local active,pending,cursor,nextAt,started,completed=false,nil,1,0,0,0
local automaticPlayer=false
local message='只读排行采集'
local received={}

local function stop(reason) active=false;pending=nil;message=reason end
local function record(kind,id,payload)
    local f=io.open('../evidence/external-pets-20260910/received-ranking.frames','ab')
    if not f then stop('采集文件不可写');return false end
    local raw=table.concat({tostring(os.time()),kind,tostring(id),payload},'|')
    local ok=f:write(tostring(#raw)..':'..raw);f:close()
    if not ok then stop('采集写入失败');return false end
    return true
end
function StartPetRankingCollection()
    if active then stop('排行采集暂停');return end
    if sendFlag or #pet<1 or #pet>2048 or petbiliType~=1 then message='请先载入战宠排行';return end
    local fields={'L',tostring(#pet)}
    for _,p in ipairs(pet) do fields[#fields+1]=p.name;fields[#fields+1]=tostring(p.index) end
    local catalog=table.concat(fields,'|')..'|'
    received={};completed=0
    local history=io.open('../evidence/external-pets-20260910/received-ranking.frames','rb')
    if history then
        local bytes=history:read(2097153);history:close()
        if not bytes or #bytes>2097152 then message='排行记录过大';return end
        local offset,relevant=1,false
        while offset<=#bytes do
            local colon=bytes:find(':',offset,true)
            local length=colon and tonumber(bytes:sub(offset,colon-1))
            if not colon or colon-offset>7 or not length or length<1 or length>262144 or colon+length>#bytes then
                message='排行记录不完整';return
            end
            local kind,id,payload=bytes:sub(colon+1,colon+length):match('^%d+|(%w+)|(%d+)|(.*)$')
            id=tonumber(id)
            if kind=='catalog' then
                relevant=payload==catalog
                if not relevant then received={};completed=0 end
            elseif relevant and id and (kind=='rank' or kind=='empty') and not received[id] then
                received[id]=true;completed=completed+1
            end
            offset=colon+length+1
        end
    end
    if not record('catalog',0,catalog) then return end
    cursor=1;pending=nil;nextAt=os.time();active=true
    while pet[cursor] and received[pet[cursor].index] do cursor=cursor+1 end
    if cursor>#pet then stop('排行采集完成 '..completed..'/'..#pet) end
end
function selectPet(index,display)
    if active then stop('手动选宠，采集暂停') end
    return choose(index,display)
end
function selectPlayer(index,display)
    if active and not automaticPlayer then stop('手动选排名，采集暂停') end
    if active and automaticPlayer then
        local chosen
        for i=1,7 do
            local p=playerName[i]
            if p and p.name and p.name~='' and p.index and p.index~='' and p.index~='0' then chosen=i;break end
        end
        if not chosen then pending.empty=true;sendFlag=false;return end
        return choosePlayer(chosen,-999)
    end
    return choosePlayer(index,display)
end
local function acknowledge(kind,payload)
    if not record(kind,pending.index,payload) then return end
    received[pending.index]=true
    completed=completed+1;cursor=cursor+1;pending=nil;nextAt=os.time()+2
    while pet[cursor] and received[pet[cursor].index] do cursor=cursor+1 end
    if cursor>#pet then stop('排行采集完成 '..completed..'/'..#pet) end
end
function WindowInit(payload)
    local kind=payload:sub(1,2)
    if active and kind=='L|' then stop('目录更新，采集暂停') end
    if active and pending and kind=='P|' then
        if pending.phase~='players' then stop('回复顺序不符，采集暂停')
        else pending.phase='stats';started=os.time() end
    end
    automaticPlayer=active and pending and kind=='P|' or false
    init(payload)
    automaticPlayer=false
    if active and pending and pending.empty and kind=='P|' then
        -- This records the absence of all seven slots in the actual P reply,
        -- not an invented S response. No query with an empty UID is sent.
        acknowledge('empty','P|'..petbiliType..'|0|0|0|0|0|0|0');return
    end
    if active and pending and kind=='S|' then
        if pending.phase~='stats' or petNameSelectIndex~=cursor or pet[cursor].index~=pending.index then
            stop('排行回复不匹配，采集暂停');return
        end
        -- S contains public pet statistics only, unlike the preceding P reply.
        acknowledge('rank',payload)
    end
end
function ServerWindowType()
    local result=draw()
    if active then
        if pending and os.time()-started>=15 then stop('排行等待超时，采集暂停')
        elseif not pending and os.time()>=nextAt and not sendFlag then
            local p=pet[cursor]
            if not p or petbiliType~=1 then stop('排行类型改变，采集暂停')
            else pending={index=p.index,phase='players'};started=os.time();choose(cursor,-999) end
        end
    end
    local label=active and ('暂停排行 '..completed..'/'..#pet) or message
    sa.StockDispBuffer(x+180,y+215,107,61868,1,'StartPetRankingCollection')
    sa.StockFontBuffer(x+110,y+206,2,4,label,0,14)
    return result
end
function PetRankingCollectorStatus() return active,completed,pending and pending.phase,message end
