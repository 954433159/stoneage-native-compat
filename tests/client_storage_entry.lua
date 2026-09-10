-- Run the generated NPC relay and the actual 6021 personal-storage renderer.
local root=arg[1] or 'client'
local function xy(path)
    local f=assert(io.open(path,'rb'));local s=f:read('*a');f:close()
    local steps={81,94,66,5,9,35,71,13,26}
    for i=#steps,1,-1 do
        local cut=#s-math.floor(#s*steps[i]/100)
        s=s:sub(cut+1):gsub('.',function(c)return string.char(255-c:byte())end)..s:sub(1,cut):reverse()
    end
    assert(loadstring(s,'@'..path))()
end
local function noop()end
local cfg,images,packets,slots={},{},{},{}
local function token(s,n)
    local fields={};for v in (s..'|'):gmatch('(.-)|') do fields[#fields+1]=v end
    return fields[n] or ''
end
char={getConfig=function(k)return cfg[k] or 0 end,setConfig=function(k,v)cfg[k]=v end,
    getAction=function()return 1 end,getPc=function()return 1 end,
    getItem=function(_,slot,k)
        slots[slot]=true
        if k==cons.item.useFlag or k==cons.item.pile then return 1 end
        if k==cons.item.graNo then return 20200 end
        return 0
    end}
win={getWinSizeX=function()return 1024 end,getWinSizeY=function()return 600 end,
    showTitleVip=noop,CloseWindow=noop}
sa={getLuaPath=function()return root..'/lua' end,DeathMenuAction=noop,
    MakeWindowDisp=function()return 1 end,closeWnd=noop,DeathAction=noop,hideItemDisp=noop,
    getTextLength=function(s)return #s end,StockFontBuffer=noop,
    StockDispBuffer=function(...)images[#images+1]={...};return 1 end,
    getStringToken=token,getIntegerToken=function(s,n)return tonumber(token(s,n)) or 0 end,
    getItemDispStaus=function()return 0 end,getTickCount=function()return 10000 end,
    drawScrollPanelPrompt=noop,drawWnd=noop,PlaySe=noop}
sp={CreateScrollPanel=noop,SetScrollMode=noop,SetScrollY=noop,SetScrollX=noop,
    SetScrollDir=noop,SetScrollHeight=noop,StockDispBuffer=noop,StockFontBuffer=noop}
bit={lshift=function(a,b)return a*2^b end,bor=function(a,b)return a+b end,
    band=function(a,b)return math.floor(a/b)%2*b end}
lssproto={menuSend=function(...)packets[#packets+1]={...}end}
xy(root..'/lua/menu.xy')
local openItems=openNativePersonalItems
itemWnd=assert(loadfile(root..'/lua/itemwnd.cf'))();itemWnd.data()
cfg[cons.config.MenuToggleFlag]=2^28
cfg[cons.config.pActMenuWnd2]=99
local closedPrevious=false
sa.DeathMenuAction2=function()closedPrevious=true;cfg[cons.config.pActMenuWnd2]=0 end
sa.CallFunction=function(name)
    if name=='openNativePersonalItems' then openItems() end
end
xy(root..'/lua/win/1199.xy');main();WindowInit('personal-item');CreateWinType()
assert(ServerWindowType()==0,'relay closes its WN before opening the item menu')
assert(closedPrevious,'replace an existing menu through native action cleanup')
assert(cfg[cons.config.WndPage]==2 and cfg[cons.config.MenuToggleFlag]==2^27)
itemWnd.proc()
assert(#packets==1 and packets[1][1]==2 and packets[1][2]=='O|' and packets[1][3]=='itemDepotData')
itemDepotData(2,'L|0|21|300|')
for page=1,3 do
    slots={};images={};changebeibao(page);itemWnd.proc()
    local count,lo,hi=0,1000,-1
    for slot in pairs(slots) do count=count+1;lo=math.min(lo,slot);hi=math.max(hi,slot) end
    assert(count==15 and lo==9+(page-1)*15 and hi==23+(page-1)*15,'each bag page must draw exactly 15 slots')
end
print('PASS NPC entry opens real personal-storage O request and three 15-slot bag pages')
