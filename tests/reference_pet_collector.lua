local now,requests,records=0,{},{}
os={time=function()return now end}
io={open=function(path,mode)
    assert(path=='../evidence/external-pets-20260910/received-growth.frames')
    if mode=='rb' then return {read=function()return table.concat(records)end,close=function()end} end
    assert(mode=='ab')
    return {write=function(_,s)records[#records+1]=s;return true end,close=function()end}
end}
x,y=492,300
petlist={{name='甲',index=10},{name='乙',index=20}}
sendFlag=false;petData={}
function WindowInit(payload)
    if payload:sub(1,2)=='D|' then petData.name=payload:match('^D|[^|]*|([^|]*)|');sendFlag=false end
end
function ShowPetQueryProc()end
function selectPet(index)requests[#requests+1]=petlist[index].index;sendFlag=true end
sa={StockDispBuffer=function()end,StockFontBuffer=function()end}
assert(loadfile('native/reference-pet-collector.lua'))()
StartPetCollection();ShowPetQueryProc()
assert(#requests==1 and requests[1]==10)
for i=1,10 do ShowPetQueryProc() end
assert(#requests==1,'must wait for the actual response')
WindowInit('D|1|甲|');now=1;ShowPetQueryProc();assert(#requests==1,'must respect spacing')
now=2;ShowPetQueryProc();assert(#requests==2 and requests[2]==20)
WindowInit('D|1|乙|');local active,count=PetCollectorStatus()
assert(not active and count==2 and #records==3,'only acknowledged details count')
now=20;ShowPetQueryProc();assert(#requests==2,'complete collector cannot keep querying')
StartPetCollection();ShowPetQueryProc()
assert(not PetCollectorStatus() and #requests==2,'completed logs must not be queried again')
records={};StartPetCollection();ShowPetQueryProc();WindowInit('D|1|错误|')
local active,count,_,_,conflicts=PetCollectorStatus()
assert(active and count==0 and conflicts==1 and records[2]:find('|conflict|10|',1,true),
    'mismatch must be quarantined, never recorded as the requested species')
StartPetCollection();StartPetCollection();now=22;ShowPetQueryProc()
assert(requests[#requests]==20,'resume must skip the quarantined species')
WindowInit('D|1|乙|')
records={};StartPetCollection();ShowPetQueryProc();now=40;ShowPetQueryProc()
assert(not PetCollectorStatus(),'timeout must pause without retry flood')
records={};sendFlag=false;StartPetCollection();ShowPetQueryProc();selectPet(2,1)
assert(not PetCollectorStatus(),'ordinary manual selection pauses collection')
records={'20:partial'};sendFlag=false;StartPetCollection()
assert(not PetCollectorStatus(),'incomplete logs cannot be appended over')
print('PASS original request path, pacing, quarantine, resume, completion, timeout and manual pause')
