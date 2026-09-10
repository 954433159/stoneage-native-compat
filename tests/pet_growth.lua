-- Real runtime menu script and native resource tables; no DB or player writes.
local function split(s,sep)
    local a={};for v in (s..sep):gmatch('(.-)'..sep) do a[#a+1]=v end;return a
end
local rows,byid={},{ }
local columns={['编号']=7,['初始值']=8,['成长率']=9,['体力']=10,['腕力']=11,
    ['耐力']=12,['速度']=13,['地']=16,['水']=17,['火']=18,['风']=19,
    ['形象']=37,['宠物标记']=38,['等级限制']=55}
for i=1,7 do columns['技能'..i]=25+i end
for line in io.lines('runtime/gmsv/data/enemybase1.txt') do
    local row=split(line,',');local id=tonumber(row[7])
    -- ENEMY_readBase skips a leading # before parsing the numeric ID.
    if id and line:sub(1,1)~='#' then rows[#rows+1]=row;byid[id]=byid[id] or (#rows-1) end
end
enemytemp={getEnemyTempArrayFromTempNo=function(id)return byid[id] or -1 end,
    getEnemyTempNum=function()return #rows end,
    getInt=function(a,k)return tonumber(assert(rows[a+1])[assert(columns[k],k)]) or 0 end,
    getChar=function(a,k)assert(k=='名字');return rows[a+1][1] end}
local packets,notices={},{}
char={getWorkInt=function(i,k)if k=='对象' then return i+1000 end;return 0 end,
    check=function(i)return i>=0 and 1 or 0 end,getFd=function()return 7 end,
    setFunctionPointer=function()end,
    TalkToCli=function(_,_,s)notices[#notices+1]=s end,
    getPet4v=function(v,s,t,d)
        return table.concat({math.floor((v*4+s+t+d)*.01),
            math.floor(s*.01+t*.01*.1+v*.01*.1+d*.01*.05),
            math.floor(t*.01+s*.01*.1+v*.01*.1+d*.01*.05),math.floor(d*.01)},'|')
    end}
local nextnpc=20
npc={CreateNpc=function()nextnpc=nextnpc+1;return nextnpc end}
net={getloginmark=function()return 4 end}
local function windowPacket(mode,...)
    local packet={...};packet.mode=mode;packets[#packets+1]=packet
    assert(#packet[6]<10240,'window payload exceeds the original client LuaErrorWnMsg buffer and overwrites chat state')
end
lssproto={windows=function(...)windowPacket('open',...)end,
    windowsupdate=function(...)windowPacket('update',...)end}
petskill={getPetskillArray=function(id)return id end,check=function(id)return id>0 and 1 or 0 end,
    getChar=function(id,k)return k=='名称' and ('技能'..id) or 'native skill description' end}
assert(loadfile('runtime/gmsv/data/ablua/samenu.lua'))();main()
FreeSaMenu(1,119)
assert(#packets==1 and packets[1][2]==1027,'growth button must open original 1027 window')
assert(packets[1].mode=='open','initial growth entry must create its window')
local firstFrame=packets[1][6];local parts={firstFrame}
local head=split(firstFrame,'|');assert(head[1]=='C' and tonumber(head[2])==1)
local obj=packets[1][5]-1000
local body={firstFrame:match('^C|%d+|%d+|%d+|(.*)$')}
for page=2,tonumber(head[3]) do
    AppPetGrowthWindow(obj,1,1,0,'C|'..page..'|')
    local p=packets[#packets];assert(p.mode=='update','subsequent parts must not recreate the window')
    parts[page]=p[6];local h=split(p[6],'|')
    assert(tonumber(h[2])==page and h[3]==head[3] and h[4]==head[4])
    body[page]=p[6]:match('^C|%d+|%d+|%d+|(.*)$')
end
local list='L|'..head[4]..'|'..table.concat(body);local f=split(list,'|')
assert(f[1]=='L' and tonumber(f[2])>100,'native catalog must not be an empty success reply')
assert(#list>10240 and #parts>1,'the complete catalog must cross the actual legacy buffer boundary')
local first=tonumber(f[4])
local listed={}
for i=1,tonumber(f[2]) do listed[tonumber(f[4+2*(i-1)])]=f[3+2*(i-1)] end
for _,id in ipairs({957,958,959,960,1180,1181}) do
    assert(listed[id], 'native pet template omitted from growth catalog: '..id)
end
assert(not listed[4556], 'same-name alternate template must not replace established 1047')
for id,image in pairs({[335]=101501,[985]=101438,[986]=101437,[1047]=101442,[1048]=101443}) do
    AppPetGrowthWindow(obj,1,1,0,'G|'..id..'|')
    assert(packets[#packets].mode=='update','pet selection must update the existing window, not reopen and reset its scroll')
    local row=split(packets[#packets][6],'|')
    assert(tonumber(row[2])==image,'beast must use its verified target sprite, not a rider: '..id)
    assert(row[3]==rows[byid[id]+1][1], 'template identity must remain unchanged')
end
AppPetGrowthWindow(obj,1,1,0,'G|'..first..'|')
local detail=packets[#packets][6];f=split(detail,'|')
local detailMode=packets[#packets].mode
assert(f[1]=='D' and tonumber(f[2])>0 and f[3]~='')
assert(tonumber(f[51])==140,'configured baseline must be labelled with its real level')
for i=12,15 do assert(tonumber(f[i])<=tonumber(f[i+4]));assert(tonumber(f[i+8])>=tonumber(f[i])) end
local n=#packets
AppPetGrowthWindow(obj,1,1,0,'G|99999999|')
AppPetGrowthWindow(obj,1,1,0,'G|1|junk')
AppPetGrowthWindow(obj,1,2,0,'G|'..first..'|')
AppPetGrowthWindow(obj,1,1,0,'C|0|')
AppPetGrowthWindow(obj,1,1,0,'C|1|')
AppPetGrowthWindow(obj,1,1,0,'C|99999|')
assert(#packets==n,'invalid IDs and commands cannot return invented data')
AppPetGrowthWindow(obj,1,0,0,'')
assert(packets[#packets][6]==firstFrame,'refresh starts the same complete native catalog')
local out=arg[1] or 'build/checks'
local function write(name,value)local p=assert(io.open(out..'/'..name,'wb'));p:write(value);p:close()end
write('pet-growth-list.txt',list);write('pet-growth-detail.txt',detail)
write('pet-growth-detail-mode.txt',detailMode)
write('pet-growth-parts.txt',tostring(#parts))
for i,part in ipairs(parts) do write('pet-growth-part-'..i..'.txt',part) end
print('PASS native pet-growth entry, catalog, detail schema, ranges and rejected invalid queries')
