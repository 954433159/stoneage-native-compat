-- Execute the generated wrapper with the actual original 6021 renderer.
local root=arg[1] or 'client'
local function noop()end
local cfg,images,labels,animations={},{},{},{}
local function token(s,n)
    local fields={};for v in (s..'|'):gmatch('(.-)|') do fields[#fields+1]=v end
    return fields[n] or ''
end
char={getConfig=function(k)return cfg[k] or 0 end,setConfig=function(k,v)cfg[k]=v end,
getAction=function()return 1 end}
win={getWinSizeX=function()return 1024 end,getWinSizeY=function()return 600 end,showTitleVip=noop}
sa={getLuaPath=function()return root..'/lua' end,DeathAction=noop,PlaySe=noop,
getStringToken=token,getIntegerToken=function(s,n)return tonumber(token(s,n)) or 0 end,
getTextLength=function(s)return #s end,
MakeAnimDisp=function(x,y,gra)animations[#animations+1]=tonumber(gra);return #animations end,
StockFontBuffer=function(x,y,p,c,text)labels[#labels+1]=text end,
StockDispBuffer=function(x,y,p,gra)images[#images+1]=gra;return 1 end}
sp={CreateScrollPanel=noop,SetScrollHeight=noop,SetScrollMode=noop,
SetScrollX=noop,SetScrollY=noop,SetScrollDir=noop,StockDispBuffer=noop,StockFontBuffer=noop}
bit={band=function()return 0 end,lshift=function()return 0 end}
lssproto={menuSend=noop}
local pet=assert(loadfile(root..'/lua/petwnd.cf'))();pet.data()
viwePetRideData(3,'9|拉奇鲁哥|1|101184|卡卡金宝|0|101187|石龟|1|128405|一|0|101184|二|0|101184|三|0|101184|四|0|101184|五|0|101184|帖鲁西卡|1|0')
selectViewRideFunc(1,0);assert(animations[1]==101184)
selectViewRideFunc(9,0)
assert(#animations==1 and pet.pActPet==0,'zero model must never create a fallback Uli animation')
nowpage=2;pet.viewPetRide()
assert(pet.selectIndex==9 and nowpage==2,'selection and page retained')
local found=false;for _,v in ipairs(labels)do if v=='骑乘形象待匹配' then found=true end end
assert(found,'missing model is explained in the existing framed window')
selectViewRideFunc(2,0);assert(animations[2]==101187)
print('PASS actual 6021 riding selection, no Uli fallback, page retention and valid model recovery')
