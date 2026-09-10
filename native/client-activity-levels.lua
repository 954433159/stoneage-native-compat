-- Local level milestones do not expire. Keep the target UI and callbacks.
local nativeActivityFont=sa.StockFontBuffer
function sa.StockFontBuffer(x,y,priority,color,text,...)
    if deadline==0 and type(text)=='string' and text:sub(1,#'活动剩余:')=='活动剩余:' then
        text='等级达成奖励长期开放'
    end
    return nativeActivityFont(x,y,priority,color,text,...)
end
