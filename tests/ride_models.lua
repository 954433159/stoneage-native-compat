-- Exercise the generated native qualification, query and real mount callbacks.
local root=arg[1] or 'runtime/gmsv'
local base,pet,flags,skin=100005,100279,16383,0
local reply
char={getInt=function(index,key)
    if key=='原图像号' or key=='图像号' then return index==1 and base or pet end
    if key=='证书骑宠' then return flags end
    return 0
end,getWorkInt=function()return 0 end,getChar=function()return 'qa' end,
getFd=function()return 7 end,setInt=function()error('must retain entitlements')end}
other={DataAndData=function(v,bit)return math.floor(v/2^bit)%2 end,
CallFunction=function()return skin end,time=function()return 1000 end}
sasql={query=function()return 0 end}
lssproto={NewSaMenu=function(fd,kind,text)assert(fd==7 and kind==3);reply=text end}
family={ShowPointListArray=function()return '' end}
other.getString=function()return '' end
dofile(root..'/data/ablua/familyridefunction.lua');main()
for avatar=1,12 do
    base=100000+(avatar-1)*20
    for _,row in ipairs({{100279,101184+(avatar-1)*8},
                         {100346,101187+(avatar-1)*8},
                         {100274,avatar<=6 and 128405 or 128406}}) do
        pet=row[1]
        assert(CheckPetRide(1,pet)==row[2],'verified riding model mismatch')
        assert(CheckPetRideByPetNo(1,pet,0)==1)
        assert(FamilyRideFunction(1,2,0)==row[2],'world mount must use same verified model')
        assert(ComFamilyRideFunction(1,2)==row[2])
    end
end
base=100005
for _,number in ipairs({120112,120135,101875,103000}) do
    pet=number
    assert(CheckPetRideByPetNo(1,pet,0)==0,'missing world model cannot fall back to Uli')
    assert(FamilyRideFunction(1,2,0)==0)
    assert(ComFamilyRideFunction(1,2)==0)
end
FreeRideQuery(1)
local fields={};for s in (reply..'|'):gmatch('(.-)|') do fields[#fields+1]=s end
local seen={}
for i=1,tonumber(fields[1]) do
    local at=2+(i-1)*3;seen[fields[at]]={tonumber(fields[at+1]),tonumber(fields[at+2])}
end
for _,name in ipairs({'帖鲁西卡','帖拉多奇斯','怀沃夫','暗帖拉所伊朵'}) do
    assert(seen[name] and seen[name][1]==1 and seen[name][2]==0,
           'keep unavailable row and earned certificate while omitting invalid sprite')
end
assert(seen['拉奇鲁哥'][2]==101184 and seen['卡卡金宝'][2]==101187 and seen['石龟'][2]==128405)
pet=100279;flags=0
assert(CheckPetRideByPetNo(1,pet,0)==0 and FamilyRideFunction(1,2,0)==0,'do not grant ride certificates')
pet=100327
assert(CheckPetRideByPetNo(1,pet,0)==1 and FamilyRideFunction(1,2,0)==101005,'ordinary green tiger retained')
skin=101006
assert(FamilyRideFunction(1,2,0)==skin,'existing outfit keeps priority')
print('PASS 12 avatars; query/world models, certificates, unavailable models, normal mount and outfit')
