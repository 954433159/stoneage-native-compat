-- A character-only level item. Unlike ITEM_SuperManItem, it never grants quest
-- flags, rebirths, pets, or allocated attributes. This native ruleset gives
-- three unallocated points per level (setup.cf SKILLUPPOINT and expup.lua).
function AppLevel140Pill(object, player, target, slot)
    if char.check(player)~=1 or target~=player or item.check(object)~=1
        or item.getInt(object,'序号')~=31020 or slot<9 or slot>53
        or char.getItemIndex(player,slot)~=object then return end
    if char.getWorkInt(player,'战斗')~=0 or char.getWorkInt(player,'交易状态')~=0
        or char.getWorkInt(player,'摆摊')~=-1 or char.getWorkInt(player,'离线')~=0 then return end
    local current=char.getInt(player,'等级')
    local limit=char.getInt(player,'限制等级')
    if current<1 or current>=140 then
        char.TalkToCli(player,-1,'当前等级无需使用140级人物经验丹。',4);return
    end
    if (limit>0 and limit<140) or char.getLevelExp(player,140)<0 then
        char.TalkToCli(player,-1,'当前人物等级上限不足140，经验丹保留。',4);return
    end
    char.setInt(player,'等级',140)
    char.setInt(player,'技能点',char.getInt(player,'技能点')+(140-current)*3)
    char.setInt(player,'当前经验',0)
    char.complianceParameter(player)
    char.setInt(player,'HP',char.getWorkInt(player,'最大HP'))
    char.setInt(player,'MP',char.getWorkInt(player,'最大MP'))
    char.DelItem(player,slot)
    char.Skillupsend(player)
    char.sendStatusString(player,'P')
    char.charSaveFromConnect(player)
    char.TalkToCli(player,-1,'等级提升至140级，请分配新增属性点。',4)
end
