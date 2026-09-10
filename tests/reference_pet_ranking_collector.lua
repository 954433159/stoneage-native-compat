local now,requests,records=0,{},{}
os={time=function()return now end}
io={open=function(path,mode)
    assert(path=='../evidence/external-pets-20260910/received-ranking.frames')
    if mode=='rb' then return {read=function()return table.concat(records)end,close=function()end} end
    assert(mode=='ab')
    return {write=function(_,s)records[#records+1]=s;return true end,close=function()end}
end}
pet={{name='甲',index=10},{name='乙',index=20}};petbiliType=1;sendFlag=false;x=0;y=0
function selectPet(index) petNameSelectIndex=index;requests[#requests+1]='G';sendFlag=true end
function selectPlayer(index)requests[#requests+1]='P';sendFlag=true end
function WindowInit(payload)
    sendFlag=false
    if payload:sub(1,2)=='P|' then
        playerName=payload:find('private%-test') and {{name='private-test-player',index='private-test-uid'}} or {}
        selectPlayer(1,-999)
    end
end
function ServerWindowType()return 27 end
sa={StockDispBuffer=function()end,StockFontBuffer=function()end}
assert(loadfile('native/reference-pet-ranking-collector.lua'))()
StartPetRankingCollection();assert(ServerWindowType()==27)
assert(#requests==1 and requests[1]=='G')
WindowInit('P|1|private-test-player|private-test-uid|')
local active,_,phase=PetRankingCollectorStatus()
assert(active and phase=='stats' and #requests==2 and requests[2]=='P')
WindowInit('S|0|');now=1;ServerWindowType();assert(#requests==2)
now=2;ServerWindowType();assert(#requests==3)
WindowInit('P|1|private-test-player|private-test-uid|');WindowInit('S|1|public-stats')
local active,count=PetRankingCollectorStatus();assert(not active and count==2 and #records==3)
for _,record in ipairs(records) do assert(not record:find('private%-test'),'no ranked player identifiers in evidence') end
StartPetRankingCollection();ServerWindowType()
assert(not PetRankingCollectorStatus() and #requests==4,'completed catalog must resume without queries')
records={};StartPetRankingCollection();ServerWindowType();WindowInit('S|0|')
assert(not PetRankingCollectorStatus(),'stats without the matching player reply cannot count')
records={};sendFlag=false;StartPetRankingCollection();ServerWindowType();now=20;ServerWindowType()
assert(not PetRankingCollectorStatus(),'unanswered query must stop')
records={};sendFlag=false;StartPetRankingCollection();ServerWindowType();selectPlayer(2,1)
assert(not PetRankingCollectorStatus(),'manual ranking selection must stop collection')
records={};sendFlag=false;StartPetRankingCollection();ServerWindowType()
local before=#requests;WindowInit('P|1||||||||||||||')
local active,count=PetRankingCollectorStatus()
assert(active and count==1 and #requests==before,'unranked pet must not query an empty UID')
assert(records[#records]:find('|empty|10|P|1|0|0|0|0|0|0|0',1,true),'empty list is P evidence, not fake S data')
StartPetRankingCollection();StartPetRankingCollection();now=22;ServerWindowType()
assert(petNameSelectIndex==2,'resume skips the known unranked species')
print('PASS original ranking flow, empty lists, privacy, resume, pacing, timeout and manual pause')
