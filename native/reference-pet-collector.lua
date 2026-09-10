-- Research-only module in client-reference, never installed in the local game.
-- Uses the original selected-pet request and only records public L/D replies.
local originalInit,originalDraw,originalSelect=WindowInit,ShowPetQueryProc,selectPet
local active,pending,cursor,nextAt,started=false,nil,1,0,0
local completed,conflicts=0,0
local received={}
local message='只读采集'
local logPath='../evidence/external-pets-20260910/received-growth.frames'

local function record(kind,id,payload)
    local stream=io.open(logPath,'ab')
    if not stream then active=false;message='采集文件不可写';return false end
    local value=table.concat({tostring(os.time()),kind,tostring(id),payload},'|')
    local ok=stream:write(tostring(#value)..':'..value)
    stream:close()
    if not ok then active=false;message='采集写入失败';return false end
    return true
end

local function stop(reason)
    active=false;pending=nil;message=reason
end

function StartPetCollection()
    if active then stop('采集已暂停');return end
    if sendFlag or #petlist<1 or #petlist>2048 then message='请等成长资料载入';return end
    local fields={'L',tostring(#petlist)}
    local names={}
    for _,pet in ipairs(petlist) do
        fields[#fields+1]=pet.name;fields[#fields+1]=tostring(pet.index)
        names[pet.index]=pet.name
    end
    local catalog=table.concat(fields,'|')..'|'
    received={};completed=0;conflicts=0
    local history=io.open(logPath,'rb')
    if history then
        local bytes=history:read(2097153);history:close()
        if not bytes or #bytes>2097152 then message='采集记录过大';return end
        local offset,relevant=1,false
        while offset<=#bytes do
            local colon=bytes:find(':',offset,true)
            local length=colon and tonumber(bytes:sub(offset,colon-1))
            if not colon or colon-offset>7 or not length or length<1 or length>262144 or colon+length>#bytes then
                message='采集记录不完整，请检查';return
            end
            local entry=bytes:sub(colon+1,colon+length)
            local kind,id,payload=entry:match('^%d+|(%w+)|(%d+)|(.*)$')
            id=tonumber(id)
            if kind=='catalog' then
                relevant=payload==catalog
                if not relevant then received={};completed=0;conflicts=0 end
            elseif relevant and names[id] and not received[id] then
                local name=payload and payload:match('^D|[^|]*|([^|]*)|')
                if kind=='detail' and name==names[id] then received[id]=true;completed=completed+1
                elseif kind=='conflict' and name and name~=names[id] then received[id]=true;conflicts=conflicts+1
                else message='采集记录身份不符';return end
            end
            offset=colon+length+1
        end
    end
    if not record('catalog',0,catalog) then return end
    cursor=1;pending=nil;nextAt=os.time();active=true
    while petlist[cursor] and received[petlist[cursor].index] do cursor=cursor+1 end
    if cursor>#petlist then stop('采集完成 '..completed..'，冲突 '..conflicts) end
end

function selectPet(index,display)
    if active then stop('手动选择，采集暂停') end
    return originalSelect(index,display)
end

function WindowInit(payload)
    originalInit(payload)
    if payload:sub(1,2)=='L|' and active then stop('目录更新，采集暂停');return end
    if active and pending and payload:sub(1,2)=='D|' then
        if not petData or not petData.name or petData.name=='' then stop('空回复，采集暂停');return end
        local mismatch=petData.name~=pending.name
        if not record(mismatch and 'conflict' or 'detail',pending.index,payload) then return end
        received[pending.index]=true
        if mismatch then conflicts=conflicts+1 else completed=completed+1 end
        cursor=cursor+1;pending=nil
        while petlist[cursor] and received[petlist[cursor].index] do cursor=cursor+1 end
        -- At least one full second between requests, even at a clock boundary.
        nextAt=os.time()+2
        if cursor>#petlist then stop('采集完成 '..completed..'，冲突 '..conflicts) end
    end
end

function ShowPetQueryProc()
    originalDraw()
    if active then
        if pending and os.time()-started>=15 then stop('等待回复超时，采集暂停')
        elseif not pending and os.time()>=nextAt and not sendFlag then
            local pet=petlist[cursor]
            if not pet then stop('目录改变，采集暂停')
            else
                pending={index=pet.index,name=pet.name};started=os.time()
                originalSelect(cursor,-999)
            end
        end
    end
    local label=active and ('暂停 '..(completed+conflicts)..'/'..#petlist..' 冲突'..conflicts) or message
    sa.StockDispBuffer(x+180,y+215,107,61868,1,'StartPetCollection')
    sa.StockFontBuffer(x+113,y+206,2,4,label,0,14)
end

function PetCollectorStatus()
    return active,completed,pending and pending.index,message,conflicts
end
