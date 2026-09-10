-- Local native outfit/title window compatibility. Ownership and requests stay
-- in the original pcwnd functions. Graphic/animated titles are not remapped.
-- Aggregate bonuses are an optional later packet. A fresh native session has
-- none; missing values must not abort the original renderer before its frame.
local originalDecorationWndProc=nativePcWnd.decorationWndProc
local drawingDecoration=false
local originalAnimation=sa.MakeAnimDisp
sa.MakeAnimDisp=function(x,y,graphic,...)
    if drawingDecoration and graphic==0 then
        local label='此装扮暂不可用'
        sa.StockFontBuffer(x-sa.getTextLength(label,18)/2,y-50,2,5,label,0,18)
        return 0
    end
    return originalAnimation(x,y,graphic,...)
end
function nativePcWnd.decorationWndProc(...)
    if type(alldressUpData)~='table' then alldressUpData={} end
    for i=1,16 do
        local value=alldressUpData[i]
        if type(value)~='number' and type(value)~='string' then
            alldressUpData[i]='—'
        end
    end
    drawingDecoration=true
    local result={pcall(originalDecorationWndProc,...)}
    drawingDecoration=false
    if not result[1] then error(result[2],0) end
    return unpack(result,2)
end

local originalSkinInit=nativePcWnd.skinInit
function nativePcWnd.skinInit(packet)
    originalSkinInit(packet)
    for _,row in ipairs(skinInfo or {}) do
        if row.graNo==0 and type(row.name)=='string' then
            -- Keep the original narrow row; the preview explains availability.
            row.name=row.name:gsub('（暂不可用）$','')
        end
    end
end

local originalTitleInit=nativePcWnd.titleInit
function nativePcWnd.titleInit(packet)
    originalTitleInit(packet)
    for _,row in ipairs(charTitle or {}) do
        if row.type==0 and NativeTitleNames[row.index] then
            row.titleGraNo=-1000-row.index
        end
    end
end

local function titleName(graphic)
    if type(graphic)=='number' and graphic%1==0 then
        return NativeTitleNames[-1000-graphic]
    end
end

local function width(name)
    return sa.getTextLength(name,18)
end

local originalPanelGraphic=sp.StockDispBuffer
sp.StockDispBuffer=function(panel,x,y,priority,graphic,...)
    local name=titleName(graphic)
    if not name then
        return originalPanelGraphic(panel,x,y,priority,graphic,...)
    end
    local hit,callback,param=...
    return sp.StockFontBuffer(panel,x-width(name)/2,y-10,2,4,name,hit or 0,18,callback or '',param or 0)
end

local originalGraphic=sa.StockDispBuffer
sa.StockDispBuffer=function(x,y,priority,graphic,...)
    local name=titleName(graphic)
    if not name then
        return originalGraphic(x,y,priority,graphic,...)
    end
    local hit,callback,param=...
    return sa.StockFontBuffer(x-width(name)/2,y-10,2,4,name,hit or 0,18,callback or '',param or 0)
end
