-- Local NPC bridge only; this window owns no inventory and handles no transfer.
function data() addr=0;openItems=false end
function main() data() end
function WindowInit(str) openItems=(str=='personal-item') end
function CreateWinType()
    addr=sa.MakeWindowDisp(win.getWinSizeX()/2-12,win.getWinSizeY()/2,0,0,54079,-1)
    return addr
end
function ServerWindowType()
    local requested=openItems
    openItems=false
    sa.closeWnd()
    win.CloseWindow()
    addr=0
    if requested then
        sa.CallFunction('openNativePersonalItems',sa.getLuaPath()..'/menu.lua',{})
    end
    return addr
end
function ClearWin() addr=0;openItems=false end
