-- Original XY growth window consumes real server-script output, including local species.
local root=arg[1] or 'client'
local output=arg[2] or 'build/checks'
local function read(path)local f=assert(io.open(path,'rb'));local s=f:read('*a');f:close();return s end
local function xy(path)
    local s=read(path);local steps={81,94,66,5,9,35,71,13,26}
    for i=#steps,1,-1 do
        local cut=#s-math.floor(#s*steps[i]/100)
        s=s:sub(cut+1):gsub('.',function(c)return string.char(255-c:byte())end)..s:sub(1,cut):reverse()
    end
    assert(loadstring(s,'@'..path))()
end
local function noop()end
local packets,images,texts,boxes={},{},{},{}
local scrollY,windowCreates={},0
local growthWindowOpen=false
local function token(s,n)local a={};for v in (s..'|'):gmatch('(.-)|') do a[#a+1]=v end;return a[n] or '' end
win={getWinSizeX=function()return 1024 end,getWinSizeY=function()return 600 end,
    getIdWN=function()return 1024 end,getHitFontNo=function()return -1 end,
    CloseWindow=function()growthWindowOpen=false end}
sa={getLuaPath=function()return root..'/lua' end,
    getStringToken=token,getIntegerToken=function(s,n)return tonumber(token(s,n)) or 0 end,
    PlaySe=noop,DeathAction=noop,closeWnd=noop,
    MakeWindowDisp=function()growthWindowOpen=true;windowCreates=windowCreates+1;return windowCreates end,MakeAnimDisp=function()return 1 end,
    StockDispBuffer=function(...)images[#images+1]={...};return 1 end,
    StockFontBuffer=function(...)texts[#texts+1]={...};return 1 end,
    StockBoxDispBuffer=function(...)boxes[#boxes+1]={...}end,StockFontBufferWarpped=noop,getTextLength=function(s)return #tostring(s)*6 end}
sp={CreateScrollPanel=noop,SetScrollMode=noop,SetScrollX=noop,SetScrollY=function(panel,y)scrollY[panel]=y end,
    SetScrollDir=noop,SetScrollHeight=noop,StockFontBuffer=noop,StockDispBuffer=noop}
lssproto={windows=function(...)
    local p={...}
    if p[4]:match('^R|') then
        assert(not growthWindowOpen,'native select window type 1 is ignored while growth type 1027 remains open')
    end
    packets[#packets+1]=p
end}
xy(root..'/lua/win/1027.xy');main()
local partCount=tonumber(read(output..'/pet-growth-parts.txt'))
WindowInit(read(output..'/pet-growth-part-1.txt'))
assert(#packets==0,'continuation cannot be requested before the native frame exists')
CreateWinType()
ServerWindowType() -- An incomplete catalog must render a safe loading frame.
local initialFrame,initialCreates=addr,windowCreates
for i=2,partCount do
    assert(packets[#packets][4]=='C|'..i..'|','catalog continuation must follow the created frame')
    WindowInit(read(output..'/pet-growth-part-'..i..'.txt'))
end
assert(addr==initialFrame and windowCreates==initialCreates,'catalog assembly must retain the one native frame')
local complete=read(output..'/pet-growth-list.txt')
assert(#petlist==sa.getIntegerToken(complete,2),'transport must retain every catalog record')
for i,row in ipairs(petlist) do
    assert(row.name==token(complete,3+2*(i-1)) and row.index==tonumber(token(complete,4+2*(i-1))))
end
assert(#petlist>100 and #petTypeData>1)
local listed=0;for _,group in ipairs(petTypeData) do listed=listed+#group.index end
assert(listed>=#petlist,'local species must remain selectable even without foreign-client categories')
local nativeRobotGroup=false
for _,group in ipairs(petTypeInfo) do
    if group.name=='特别系' then
        for _,name in ipairs(group.data) do if name=='雷帝斯洛' then nativeRobotGroup=true end end
    end
end
assert(nativeRobotGroup,'native robot-dragon spelling must remain in the original special-species group')
assert(packets[#packets][1]==1 and packets[#packets][4]:match('^G|%d+|$'))
assert(#packets==partCount,'only continuation requests and one initial G query are allowed, without reopening')
-- Mirror native WN vs WNUpdate: only a new window invokes CreateWinType.
local frameAddr=addr;local createsBefore=windowCreates
local categories=petTypeData;local selected=petNameSelectIndex
petTypeData[#petTypeData].flag=true
sp.SetScrollY(0,280)
WindowInit(read(output..'/pet-growth-detail.txt'))
if read(output..'/pet-growth-detail-mode.txt')=='open' then CreateWinType() end
ServerWindowType()
assert(addr==frameAddr and windowCreates==createsBefore,'detail response must retain the existing native window frame')
assert(scrollY[0]==280,'selecting a pet must preserve the scrolled catalog position')
assert(petTypeData==categories and petTypeData[#petTypeData].flag,'expanded categories must survive detail refresh')
assert(petNameSelectIndex==selected and not sendFlag,'selection must remain highlighted when its detail arrives')
assert(petData.graNo>0 and petData.hp[1]>0 and petData.hp[3]>petData.hp[1])
local frame,mask,note=false,false,false
for _,r in ipairs(images)do if r[4]==61872 then frame=true elseif r[4]==51355 then mask=true end end
for _,r in ipairs(texts)do if tostring(r[5]):find('140级',1,true) then note=true end end
assert(frame and mask and note,'original growth frame, mask and labelled native level must render')
local header
for _,r in ipairs(texts)do if r[5]=='可学技能' then header=r end end
assert(header,'only a real native training route may be labelled learnable')
local beforeService=#packets
OpenLocalPetService()
assert(#packets==beforeService+1 and packets[#packets][4]:match('^R|%d+|$'), 'local cultivation must send the selected real species ID')
sendFlag=true;OpenLocalPetService()
assert(#packets==beforeService+1,'unreceived pet selections cannot open another species service')
sendFlag=false
for _,r in ipairs(boxes)do
    if r[1]<=header[1] and r[3]>header[1] and r[2]<=header[2] and r[4]>header[2] then
        assert(r[5]<112,'opaque box must render before FRONT text, which is flushed at priority 112')
    end
end
print('PASS real growth XY catalog, in-place detail, retained scroll/categories/selection, frame and level label')
