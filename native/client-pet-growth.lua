-- Retain original 1027 rendering and selection; include local catalog species.
local originalGrowthInit=WindowInit
local originalGrowthDraw=ShowPetQueryProc
local originalGrowthCreate=CreateWinType
local nativeGrowthLevel,nativeLearnable
local catalogParts
local function requestCatalogPart()
    if catalogParts and not catalogParts.requested then
        catalogParts.requested=true
        lssproto.windows(1,win.getIdWN(),0,'C|'..catalogParts.next..'|')
    end
end
function CreateWinType()
    local result=originalGrowthCreate()
    requestCatalogPart()
    return result
end
function WindowInit(str)
    if str:sub(1,2)=='C|' then
        local index,total,count,body=str:match('^C|(%d+)|(%d+)|(%d+)|(.*)$')
        index,total,count=tonumber(index),tonumber(total),tonumber(count)
        if not index or total<1 or total>128 or count<1 or count>8192 or #str>=8000 then return end
        if index==1 then
            catalogParts={next=1,total=total,count=count,parts={}}
            nativeGrowthLevel=nil;nativeLearnable=false;labelid=3
            petlist={};petTypeData={};petData={};petNameSelectIndex=-1;sendFlag=true
            originalGrowthInit('C|')
        end
        local pending=catalogParts
        if not pending or index~=pending.next or total~=pending.total or count~=pending.count then return end
        pending.parts[index]=body;pending.next=index+1;pending.requested=false
        if index<total then
            -- Part one is initialized before the native frame exists.
            if index>1 then requestCatalogPart() end
            return
        end
        str='L|'..count..'|'..table.concat(pending.parts)
        catalogParts=nil
    end
    if str:sub(1,2)=='L|' then
        nativeGrowthLevel=nil
        -- The native growth route already supplied L. The original default
        -- label switch would send an empty WN and request another opening.
        labelid=3
        local known,extra,listed={},{},{}
        for i=1,sa.getIntegerToken(str,2) do listed[sa.getStringToken(str,3+2*(i-1))]=true end
        for i=#petTypeInfo,1,-1 do
            if petTypeInfo[i].nativeGroup then table.remove(petTypeInfo,i) end
        end
        for _,group in ipairs(petTypeInfo) do
            for _,name in ipairs(group.data) do known[name]=true end
            -- The native template spells this robot dragon differently. Keep
            -- its server identity while placing it in the original category.
            if group.name=='特别系' and listed['雷帝斯洛'] and not known['雷帝斯洛'] then
                group.data[#group.data+1]='雷帝斯洛';known['雷帝斯洛']=true
            end
        end
        for i=1,sa.getIntegerToken(str,2) do
            local name=sa.getStringToken(str,3+2*(i-1))
            if not known[name] then extra[#extra+1]=name;known[name]=true end
        end
        if #extra>0 then
            petTypeInfo[#petTypeInfo+1]={priority=1000,name='本服其他宠物',data=extra,nativeGroup=true}
        end
    elseif str:sub(1,2)=='D|' then
        nativeGrowthLevel=sa.getIntegerToken(str,51)
        nativeLearnable=sa.getIntegerToken(str,52)==1
    end
    return originalGrowthInit(str)
end
function ShowPetQueryProc()
    originalGrowthDraw()
    if nativeGrowthLevel and nativeGrowthLevel>0 then
        -- Only species with an actual native training route are labelled learnable.
        -- FRONT fonts flush before priority 112; the cover must be below them.
        sa.StockBoxDispBuffer(x+225,y+13,x+350,y+38,111,0,1)
        sa.StockFontBuffer(x+232,y+17,2,0,nativeLearnable and '可学技能' or '初始技能',0,18)
        sa.StockFontBuffer(x-168,y+198,2,0,
            '1→'..nativeGrowthLevel..'级 · 0转基础理论范围',0,14)
        sa.StockDispBuffer(x+200,y+215,107,61868,1,'OpenLocalPetService')
        sa.StockFontBuffer(x+151,y+206,2,0,'本地培育',0,15)
    end
end
function OpenLocalPetService()
    if sendFlag or not nativeGrowthLevel then return end
    local selected=petlist and petlist[petNameSelectIndex]
    if selected and selected.index then
        local object=win.getIdWN()
        closeWnd()
        lssproto.windows(1,object,0,'R|'..selected.index..'|')
    end
end
