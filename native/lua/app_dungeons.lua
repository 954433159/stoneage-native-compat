-- Embedded by the recorded resource patch in the original NewFB.lua state.
-- Target 20260805 window 1131: L/N catalog, Z details, Q reward, G native entry.
local AppDungeons = {
    {{name="单人日常副本", file="data/ablua/npc/newFB/NewFB.lua",
      description="原生日常副本入口。单人挑战，每日最多完成2次。奖励由原副本战后流程发放。以下为部分奖励。",
      schedule="每日开放，0点重置次数。", rewards={20810,23800,25100,22050}}},
    {{name="荒原大魔王", file="data/ablua/npc/YamaKing/MoWang.lua",
      description="传送至原挑战NPC。每位参战成员需要1张地狱通行证；胜利返还通行证并获得随机奖励。以下为部分奖励。",
      schedule="副本线路开放时可挑战。", rewards={20900,26026,28461,20810}}},
    {{name="龙域副本", file="data/ablua/npc/hunting/Hunting.lua",
      description="保留原龙域活动规则；活动开启后方可进入。挑战幼龙与世界BOSS，奖励由原战后流程发放。以下为部分奖励。",
      schedule="每周二、周五18至20点。以原活动状态为准。", rewards={25100,25101,25111,25121,25131}}},
}

local function AppDungeonInteger(value)
    local number = tonumber(value)
    if number and number == math.floor(number) then return number end
end

local function AppDungeonRow(category, index)
    local list = AppDungeons[category]
    return list and list[index]
end

local function AppDungeonDetails(row)
    local result = row.description .. "|" .. row.schedule .. "|" .. #row.rewards
    for _, id in ipairs(row.rewards) do
        result = result .. "|" .. id .. "|" .. item.getgraNoFromITEMtabl(id)
    end
    return result
end

local function AppDungeonSend(talkerindex, payload, update)
    if not appDungeonNpc or char.check(appDungeonNpc) ~= 1 then return end
    local send = update and lssproto.windowsupdate or lssproto.windows
    send(talkerindex, 1131, 0, 1131, char.getWorkInt(appDungeonNpc, "对象"), payload)
end

local function AppDungeonList(talkerindex, category, update)
    local list = AppDungeons[category]
    if not list then return end
    local payload = (update and "N" or "L") .. "|" .. category .. "|" .. #list
    for _, row in ipairs(list) do payload = payload .. "|" .. row.name end
    AppDungeonSend(talkerindex, payload .. "|1|" .. AppDungeonDetails(list[1]), update)
end

function ShowAppDungeons(talkerindex)
    return AppDungeonList(talkerindex, 1, false)
end

function AppDungeonWindow(meindex, talkerindex, select, data)
    if select == 8 or select == 2 or char.check(talkerindex) ~= 1 then return end
    local command = other.getString(data, "|", 1)
    local category = AppDungeonInteger(other.getString(data, "|", 2))
    if command == "N" then return AppDungeonList(talkerindex, category, true) end
    if command == "Q" then
        for _, list in ipairs(AppDungeons) do
            for _, row in ipairs(list) do
                for _, id in ipairs(row.rewards) do
                    if id == category then
                        return AppDungeonSend(talkerindex, "Q|" .. id .. "|" .. item.getSecretNameFromNumber(id)
                            .. "|" .. item.getItemInfoFromNumber(id), true)
                    end
                end
            end
        end
        return
    end
    local index = AppDungeonInteger(other.getString(data, "|", 3))
    local row = AppDungeonRow(category, index)
    if not row then return end
    if command == "Z" then
        return AppDungeonSend(talkerindex, "Z|" .. index .. "|" .. AppDungeonDetails(row), true)
    end
    if command == "G" then
        if char.getWorkInt(talkerindex, "战斗") ~= 0 or char.getWorkInt(talkerindex, "组队") == 2
            or char.getInt(talkerindex, "地图号") == 117 then return end
        -- CallFunction clears its destination stack. This entry already runs in
        -- NewFB's state, so a bridge call would erase its own argument table.
        if row.file == "data/ablua/npc/newFB/NewFB.lua" then
            return AppDungeonEnter(talkerindex)
        end
        return other.CallFunction("AppDungeonEnter", row.file, {talkerindex})
    end
end

-- SQL data stays UTF-8 even under NO_BACKSLASH_ESCAPES. Never concatenate a raw name.
local function AppDungeonSqlText(value)
    local hex = (value:gsub(".", function(byte) return string.format("%02X", byte:byte()) end))
    return "CONVERT(X'" .. hex .. "' USING utf8mb4)"
end

function AppDungeonEnter(talkerindex)
    if not GetFBdata(talkerindex) then
        char.TalkToCli(talkerindex, -1, "今日副本次数已用完，或当前无法读取次数。请稍后再试。", 4)
        return
    end
    char.AllWarpToSpecificPoint(talkerindex, 3, 12, 13)
end
