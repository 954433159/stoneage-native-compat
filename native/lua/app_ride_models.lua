-- Target-client model IDs verified against the public riding query and sprites.
-- Certificate bits, expiry dates, pet objects and outfit ownership stay native.
local unavailableRideModels={[120112]=true,[120135]=true,[101875]=true,[103000]=true}
local nativeRideData=data
function data()
    nativeRideData()
    for avatar=1,12 do
        NewRideNoList[avatar][5]=101184+(avatar-1)*8 -- 拉奇鲁哥
        NewRideNoList[avatar][6]=101187+(avatar-1)*8 -- 卡卡金宝
        NewRideNoList[avatar][13]=avatar<=6 and 128405 or 128406 -- 石龟
    end
end

local nativeRideQualification=CheckPetRideByPetNo
function CheckPetRideByPetNo(meindex,petNo,kind)
    if unavailableRideModels[petNo] then return 0 end
    return nativeRideQualification(meindex,petNo,kind)
end
local nativeMount=FamilyRideFunction
function FamilyRideFunction(meindex,petindex,pethaveindex)
    if unavailableRideModels[char.getInt(petindex,"原图像号")] then return 0 end
    return nativeMount(meindex,petindex,pethaveindex)
end
local nativeMountDisplay=ComFamilyRideFunction
function ComFamilyRideFunction(meindex,petindex)
    if unavailableRideModels[char.getInt(petindex,"原图像号")] then return 0 end
    return nativeMountDisplay(meindex,petindex)
end

function FreeRideQuery(meindex)
    local count,fields=0,{}
    for i,petNo in ipairs(ridepet) do
        local graphic=CheckPetRide(meindex,petNo)
        if graphic>0 then
            count=count+1
            fields[#fields+1]=petname[i]
            -- Show the original earned qualification even while artwork is absent.
            fields[#fields+1]=nativeRideQualification(meindex,petNo,0)
            fields[#fields+1]=unavailableRideModels[petNo] and 0 or graphic
        end
    end
    lssproto.NewSaMenu(char.getFd(meindex),3,count..'|'..table.concat(fields,'|'))
    return 1
end
