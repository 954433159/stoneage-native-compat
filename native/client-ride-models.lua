-- Preserve the original riding window and its paging/qualification display.
local selectNativeRide=selectViewRideFunc
function selectViewRideFunc(index,...)
    local row=nativePetWnd.petData and nativePetWnd.petData[index]
    if row and tonumber(row.graNo)==0 then
        nativePetWnd.selectIndex=index
        if nativePetWnd.pActPet~=0 then sa.DeathAction(nativePetWnd.pActPet) end
        nativePetWnd.pActPet=0
        sa.PlaySe(203,320,240)
        return
    end
    return selectNativeRide(index,...)
end
local viewNativeRide=nativePetWnd.viewPetRide
function nativePetWnd.viewPetRide(...)
    viewNativeRide(...)
    local row=nativePetWnd.petData and nativePetWnd.petData[nativePetWnd.selectIndex]
    if row and tonumber(row.graNo)==0 then
        local x,y=nativePetWnd.x+215,nativePetWnd.y+25
        for i,label in ipairs({'骑乘形象待匹配','骑证资格仍保留'}) do
            sa.StockFontBuffer(x-sa.getTextLength(label,18)/2,y+(i-1)*24,2,5,label,0,18)
        end
    end
end
