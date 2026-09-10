-- Local acquisition policy, deliberately separate from unverified foreign routes.
-- Creation/skills live in native objects and use the ordinary SAAC save path.
local petServiceSessions={}
local petAdoptionPrice,petTeachingPrice=10000,1000
-- openServerWindow and updataServerWindow strcpy into LuaErrorWnMsg[10240].
-- Split only between complete UTF-8 records; the client requests later parts
-- after CreateWinType, since an update received before that is discarded.
local transportList,transportFrames
function AppPetGrowthFrames(list)
    if transportList==list then return transportFrames end
    local count,body=list:match('^L|([%d.]+)|(.*)$')
    count=assert(tonumber(count));assert(count>0 and count==math.floor(count))
    local chunks,chunk,records={},'',0
    for name,id in body:gmatch('([^|]+)|(%d+)|') do
        local entry=name..'|'..id..'|';assert(#entry<=7800)
        if #chunk+#entry>7800 then chunks[#chunks+1]=chunk;chunk='' end
        chunk=chunk..entry;records=records+1
    end
    assert(records==count)
    if chunk~='' then chunks[#chunks+1]=chunk end
    local frames={}
    for i,part in ipairs(chunks) do
        frames[i]=string.format('C|%d|%d|%d|',i,#chunks,count)..part
        assert(#frames[i]<8000)
    end
    transportList,transportFrames=list,frames
    return frames
end
local function petServiceOwner(talker)
    return char.getChar(talker,'账号')..'\n'..char.getChar(talker,'名字')
end
local function petServiceNotice(talker,text)
    char.TalkToCli(talker,-1,text,'黄色')
end
local function petServiceWindow(me,talker,kind,buttons,seq,text)
    lssproto.windows(talker,kind,buttons,seq,char.getWorkInt(me,'对象'),text)
end
local function petServiceList(me,talker,seq,title,rows)
    petServiceWindow(me,talker,'选择框','取消',seq,'3           '..title..'\n请选择\n\n'..table.concat(rows,'\n'))
end
local function petServiceCatalog(id)
    for _,entry in ipairs(AppExternalPetCatalog) do if entry.id==id then return entry end end
end
function AppExternalPetLearnable(id)
    local entry=petServiceCatalog(id)
    return entry and entry.skills
end
local function petServiceCurrentPet(talker,session)
    if not session.slot then return nil end
    local pet=char.getCharPet(talker,session.slot)
    if char.check(pet)~=1 or char.getChar(pet,'唯一编号')~=session.uid
        or char.getInt(pet,'宠ID')~=session.entry.id then return nil end
    return pet
end
function AppExternalPetWindow(me,talker,seq,button,str)
    if button==2 or button==8 then petServiceSessions[talker]=nil;return end
    local requested=seq==1 and type(str)=='string' and tonumber(str:match('^R|(%d+)|$'))
    if requested then
        local entry=petServiceCatalog(requested)
        if not entry then petServiceNotice(talker,'此宠物尚无本地培育配置。');return end
        petServiceSessions[talker]={owner=petServiceOwner(talker),entry=entry,expires=os.time()+120,stage=10}
        petServiceList(me,talker,10,entry.name..' · 本地培育',
            {'领养一只（'..petAdoptionPrice..'石币）','训练身上同种宠物（每技'..petTeachingPrice..'石币）','查看技能与获取说明'})
        return
    end
    local session=petServiceSessions[talker]
    if not session or session.owner~=petServiceOwner(talker) or os.time()>session.expires or seq~=session.stage then
        petServiceSessions[talker]=nil;return
    end
    local choice=type(str)=='string' and tonumber(str:match('^([1-9]%d*)$'))
    if seq==10 then
        if choice==1 then
            session.stage=11
            petServiceWindow(me,talker,'对话框','确定|取消',11,
                '领养1级 '..session.entry.name..'，需要'..petAdoptionPrice..'石币。\n这是本服培育途径，费用为本服配置。\n确认后生成真实宠物，资质由原生随机生成。')
        elseif choice==2 then
            local rows={};session.pets={}
            for slot=0,4 do
                local pet=char.getCharPet(talker,slot)
                if char.check(pet)==1 and char.getInt(pet,'宠ID')==session.entry.id then
                    rows[#rows+1]=char.getChar(pet,'名字')..'（'..char.getInt(pet,'等级')..'级，第'..(slot+1)..'位）'
                    session.pets[#rows]={slot=slot,uid=char.getChar(pet,'唯一编号')}
                end
            end
            if #rows==0 then petServiceNotice(talker,'身上没有这种宠物。');petServiceSessions[talker]=nil;return end
            session.stage=12;petServiceList(me,talker,12,'选择训练宠物',rows)
        elseif choice==3 then
            session.stage=16
            local text='本地获取：宠物→成长→选择宠物→本地培育。\n领养'..petAdoptionPrice..'石币；学习每技'..petTeachingPrice..'石币。\n外服具体获取途径尚未实证。\n当前可以训练：'
            for _,id in ipairs(session.entry.skills) do
                local array=petskill.getPetskillArray(id)
                if array>=0 then text=text..petskill.getChar(array,'名称')..' ' end
            end
            if session.entry.pending~='' then text=text..'\n外服已收录、尚未接入：'..session.entry.pending end
            petServiceWindow(me,talker,'对话框','确定',16,text)
        end
    elseif seq==11 and button==1 then
        petServiceSessions[talker]=nil -- duplicate confirmations cannot create twice
        local free=false
        for slot=0,4 do if char.check(char.getCharPet(talker,slot))~=1 then free=true;break end end
        if not free then petServiceNotice(talker,'宠物栏已满，未扣石币。');return end
        local money=char.getInt(talker,'石币')
        if money<petAdoptionPrice then petServiceNotice(talker,'石币不足。');return end
        local pet=char.AddPetTempNo(talker,session.entry.id,1)
        if not pet or char.check(pet)~=1 then petServiceNotice(talker,'宠物创建失败，未扣石币。');return end
        char.setInt(talker,'石币',money-petAdoptionPrice);char.sendStatusString(talker,'P')
        petServiceNotice(talker,'已领养'..session.entry.name..'，扣除'..petAdoptionPrice..'石币。')
    elseif seq==12 and choice and session.pets[choice] then
        session.slot=session.pets[choice].slot;session.uid=session.pets[choice].uid
        local pet=petServiceCurrentPet(talker,session)
        if not pet then petServiceSessions[talker]=nil;return end
        session.skills={};local rows={}
        for _,id in ipairs(session.entry.skills) do
            local learned=false
            for slot=0,6 do if char.getPetSkill(pet,slot)==id then learned=true end end
            local array=petskill.getPetskillArray(id)
            if not learned and array>=0 and petskill.check(array)==1 then
                session.skills[#session.skills+1]=id
                rows[#rows+1]=petskill.getChar(array,'名称')..'（'..petTeachingPrice..'石币）'
            end
        end
        if #rows==0 then petServiceNotice(talker,'已学会当前开放的全部技能。');petServiceSessions[talker]=nil;return end
        session.stage=13;petServiceList(me,talker,13,'选择宠物技能',rows)
    elseif seq==13 and choice and session.skills[choice] then
        session.skill=session.skills[choice];session.stage=14
        local array=petskill.getPetskillArray(session.skill)
        petServiceWindow(me,talker,'对话框','确定|取消',14,
            petskill.getChar(array,'名称')..'\n'..petskill.getChar(array,'注释')..'\n费用：'..petTeachingPrice..'石币。\n使用空技能位；已学技能保持。')
    elseif seq==14 and button==1 then
        petServiceSessions[talker]=nil
        local pet=petServiceCurrentPet(talker,session)
        if not pet then petServiceNotice(talker,'宠物已改变，请重新选择。');return end
        local free
        for slot=0,math.min(7,char.getInt(pet,'宠技位'))-1 do
            local skill=char.getPetSkill(pet,slot)
            if skill==session.skill then petServiceNotice(talker,'已经学会此技能，未扣石币。');return end
            if skill<1 and not free then free=slot end
        end
        if free==nil then petServiceNotice(talker,'宠物技能位已满，未扣石币。');return end
        local money=char.getInt(talker,'石币')
        if money<petTeachingPrice then petServiceNotice(talker,'石币不足。');return end
        char.setPetSkill(pet,free,session.skill)
        if char.getPetSkill(pet,free)~=session.skill then petServiceNotice(talker,'技能学习失败，未扣石币。');return end
        char.setInt(talker,'石币',money-petTeachingPrice)
        char.sendStatusString(talker,'W'..session.slot);char.sendStatusString(talker,'P')
        petServiceNotice(talker,'宠物已学会新技能，扣除'..petTeachingPrice..'石币。')
    elseif seq==16 then
        petServiceSessions[talker]=nil
    end
end
