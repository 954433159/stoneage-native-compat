-- Execute the actual target 6021 pcwnd and XY title resources with the reference VM.
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
xy(root..'/lua/title.xy');data()
assert(GetTitleStr(7)=='[style c=4]初心使者[/style]', 'native title 7 must not be a foreign image number')
assert(GetTitleStr(301)=='[style c=16]英勇青铜Ⅴ[/style]', 'unrelated rank title retained')
local titleworld=GetTitleStr
local function field(s,n)
    local fields={};for part in (s..'|'):gmatch('(.-)|') do fields[#fields+1]=part end
    return fields[n] or ''
end
local drawn,images,packets,animations={},{},{},{}
local wire
lssproto={util_mkbuffer=function()wire={}end,
    util_mkint=function(v)assert(type(v)=='number');wire[#wire+1]=v;return v end,
    util_SendMesg=function(id)packets[#packets+1]={id,wire[1]}end}
local cfg={}
char={getConfig=function(k)return cfg[k] or 0 end,setConfig=function(k,v)cfg[k]=v end,
      getPc=function()pc={graNo=100000};return pc end}
win={getWinSizeX=function()return 1024 end,getWinSizeY=function()return 600 end,
     print=function()end,showTitleVip=function()end}
sa={getLuaPath=function()return root..'/lua' end,
    getStringToken=function(s,n)return field(s,n) end,
    getIntegerToken=function(s,n)return tonumber(field(s,n)) or 0 end,
    DeathAction=function()end,PlaySe=function()end,MakeAnimDisp=function(...)
        animations[#animations+1]={...};return 1 end,
    getTextLength=function(s,size)return #s/3*size end,
    StockFontBuffer=function(...)drawn[#drawn+1]={'sa',...};return 1 end,
    StockDispBuffer=function(...)images[#images+1]={'sa',...};images[#images].argc=select('#',...);return 1 end,
    menuSend=function(...)packets[#packets+1]={...}end}
sp={StockFontBuffer=function(...)drawn[#drawn+1]={'sp',...}end,
    StockDispBuffer=function(...)images[#images+1]={'sp',...}end,
    CreateScrollPanel=function()end,SetScrollHeight=function()end}
local pcwnd=assert(loadfile(root..'/lua/pcwnd.cf'))();pcwnd.data();pcwnd.clearTitle()
pcwnd.titleInit('S|2|0|32572|0|0|7|0|0|0|32573|0|0|8|0|0')
assert(titleLen==2 and charTitle[1].index==7 and charTitle[2].index==8)
assert(charTitle[1].titleGraNo==-1007 and charTitle[2].titleGraNo==-1008)
pcwnd.titleInit('N|0|7');assert(nowCharTitleIndex==7)
-- The original renderer and its callback names are used, not a duplicate UI.
selectDecorationFlag=3;selectTitleIndex=1
-- A fresh client has received no optional aggregate dress-up packet. The
-- original renderer used to abort after drawing rows, before the frame.
assert(alldressUpData==nil)
pcwnd.decorationWndProc()
local frame,mask=false,false
for _,row in ipairs(images) do
    if row[1]=='sa' and row[5]==54079 then frame=true end
    if row[1]=='sa' and row[5]==51355 then mask=true end
end
assert(frame and mask,'fresh dress-up view must finish drawing its frame and backdrop')
assert(alldressUpData[1]=='—','unreported aggregate must not pretend to be a measured zero')
alldressUpData[1]='17';pcwnd.decorationWndProc()
assert(alldressUpData[1]=='17','reported aggregate must survive the display guard')
local first,second,preview=false,false,false
for _,row in ipairs(drawn) do
    if row[1]=='sp' and row[7]=='初心使者' then
        assert(row[10]=='selectTitleFunc' and row[11]==1);first=true
    elseif row[1]=='sp' and row[7]=='认证妹子' then
        assert(row[10]=='selectTitleFunc' and row[11]==2);second=true
    elseif row[1]=='sa' and row[6]=='初心使者' then preview=true end
end
assert(first and second and preview,'actual original renderer must draw native names and selectable rows')
selectTitleFunc(2);sendTitleData(charTitle[2].index);assert(packets[#packets][1]==242 and packets[#packets][2]==8)
hideTitle();assert(packets[#packets][1]==242 and packets[#packets][2]==0)
pcwnd.clearTitle();pcwnd.titleInit('S|0');assert(titleLen==0,'character switch must clear another character titles')
pcwnd.titleInit('A|0|32572|0|0|7|0|0');assert(titleTotal==1 and charTitle[1].titleGraNo==-1007)
sa.StockDispBuffer(10,20,106,61159,0);assert(images[#images][5]==61159 and images[#images].argc==5,'other graphics and exact argument count unchanged')
-- An unavailable owned skin must keep its row/frame without making the engine
-- draw its generic Uli fallback. A verified skin still uses the native preview.
skinInfo={};for i=1,64 do skinInfo[i]={} end;skinAddr=0
pcwnd.skinInit('Q|I|2|光之刃剑（暂不可用）|0|0|1|0|0|黑帮教父A|108025|0|2|0|0')
assert(skinInfo[1].name=='光之刃剑','unavailable marker belongs in the preview, not overflowing a narrow name row')
pcwnd.skinInit('Q|A|0');selectDecorationFlag=1;selectSkinFunc(1)
local beforeAnimations=#animations;local beforeText=#drawn
pcwnd.decorationWndProc()
assert(#animations==beforeAnimations,'unavailable skin must not create a Uli animation')
local unavailable=false
for i=beforeText+1,#drawn do
    if drawn[i][1]=='sa' and drawn[i][6]=='此装扮暂不可用' then unavailable=true end
end
assert(unavailable,'unavailable skin needs a visible explanation')
selectSkinFunc(2);pcwnd.decorationWndProc()
assert(animations[#animations][3]==108025,'verified skin keeps the original animated preview')
sa.MakeAnimDisp(1,2,0,0)
assert(animations[#animations][3]==0,'preview guard must not change other windows')
print('PASS original 6021 title rows, native text, callbacks, hide, empty switch, incremental ownership')
