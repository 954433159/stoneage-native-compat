-- Called by the local NPC entry after the normal WN dialog has closed.
-- All drawing, bag pages and O/X/C/B/H requests remain in original itemwnd.cf.
function openNativePersonalItems()
    sa.DeathMenuAction()
    sa.DeathMenuAction2()
    local cx=win.getWinSizeX()/2-12
    local cy=win.getWinSizeY()/2
    char.setConfig(cons.config.pActMenuWnd2,sa.MakeWindowDisp(cx,cy,0,0,54079,-1))
    char.setConfig(cons.config.MenuToggleFlag,bit.lshift(1,27))
    char.setConfig(cons.config.WndPage,2)
    char.setConfig(cons.config.WndProcNo,0)
end
