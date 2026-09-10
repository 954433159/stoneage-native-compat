-- Text title names come from the native definition, not foreign image IDs.
local originalTitleText=GetTitleStr
function GetTitleStr(id)
    local name=NativeTitleNames[id]
    if name then return '[style c=4]'..name..'[/style]' end
    return originalTitleText(id)
end

-- Read-only access to the original mapping for protocol regression checks.
NativeOriginalTitleStr=originalTitleText
