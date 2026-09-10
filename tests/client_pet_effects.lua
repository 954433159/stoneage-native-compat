-- Execute the actual target 6021 pet effect renderer with the reference VM.
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
local drawn,images,packets={},{},{}
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
    DeathAction=function()end,PlaySe=function()end,MakeAnimDisp=function()return 1 end,
    getTextLength=function(s,size)return #s/3*size end,
    StockFontBuffer=function(...)drawn[#drawn+1]={'sa',...};return 1 end,
    StockDispBuffer=function(...)images[#images+1]={'sa',...};images[#images].argc=select('#',...);return 1 end,
    menuSend=function(...)packets[#packets+1]={...}end}
sp={StockFontBuffer=function(...)drawn[#drawn+1]={'sp',...}end,
    StockDispBuffer=function(...)images[#images+1]={'sp',...}end,
    CreateScrollPanel=function()end,SetScrollHeight=function()end}

local function noop() end
for _,k in ipairs({'SetScrollMode','SetScrollY','SetScrollX','SetScrollDir','SetScrollHeight'}) do sp[k]=noop end
char.getPet=function(i,k) if k==cons.pet.useFlag then return i==0 and 1 or 0 elseif k==cons.pet.graNo then return 100251 elseif k==cons.pet.name then return '新手金格萨贝鲁' else return 0 end end
char.getAction=function(i,k) return actions[i] or 0 end
lssproto.menuSend=noop
sa.StockBoxDispBuffer=noop
sa.noMouseClick=noop
bit={band=function() return 0 end,lshift=function(a,b) return 0 end}
actions={};local nextid=10
sa.MakeAnimDisp=function(x,y,gra) nextid=nextid+1;actions[nextid]=gra;return nextid end
sa.DeathAction=function(id) actions[id]=nil end
local petwnd=assert(loadfile(root..'/lua/petwnd.cf'))();petwnd.data()

petwnd.init(3)
local f=assert(io.open((arg[2] or 'build/checks')..'/pet-effects-reply.txt','rb'));local reply=f:read('*a');f:close();viwePetEffData(19,reply)
for _,id in ipairs({65,66,67}) do
 images={}
 selectEffectFunc(id,0)
 petwnd.viewPetEffect()
 local frame,mask=false,false
 for _,row in ipairs(images) do
  if row[1]=='sa' and row[5]==61392 then frame=true end
  if row[1]=='sa' and row[5]==51355 then mask=true end
 end
 assert(frame and mask,'body-effect preview must complete its original frame')
end
assert(petwnd.peteffdata[1].gongji==109900 and petwnd.peteffdata[1].gongji2==125002)
assert(petwnd.peteffdata[5].gongji==109905,'five-slot body/aura reply must preserve native data')
print('PASS real 6021 previews 65/66/67, frames, backdrop and sparse body/aura state')
