-- Exercise the original login policy; isolate SQL/player dependencies only.
dofile('runtime/gmsv/data/ablua/logincheck.lua')
local now, result, marked = 1800000000, nil, nil
config = {getFdnum=function() return 16 end, getServernumber=function() return 1 end,
          getGameservername=function() return 'LocalStone' end}
net = {getMac=function() return 'local-fixture' end, getMac2=function() return '20260805' end,
       getIP=function() return '127.0.0.1' end, getUse=function() return 0 end,
       setloginmark=function(_, value) marked=value end}
other = {atoi=function(x) return tonumber(x) or 0 end, time=function() return now end,
         md5=function(value) return 'digest:'..value end}
char = {getPlayerMaxNum=function() return 100 end}
sasql = {query=function() return 1 end, free_result=function() end, store_result=function() end,
         num_rows=function() return 0 end}
lssproto = {ClientLogin=function(_, value) result=value end}

local function attempt(platform, protocol, timestamp, bad_signature)
    result, marked = nil, nil
    local payload
    if protocol == 1 then
        payload='fixture'..'secret'..timestamp..'7b36fa67c2f982245c94675e45bcc15c'
    else
        payload=timestamp..'fixture'..'secret'..'5416d7cd6ef195a0f7622a9c56b55e85'
    end
    return FreeLoginCheck(5,'afixture','asecret','127.0.0.1','local-fixture','20260805',
                          platform,timestamp,bad_signature and 'invalid' or other.md5(payload),'',protocol)
end

assert(attempt('win',2,now,false)==1, 'Windows AppClientLogin (151) rejected: '..tostring(result))
assert(marked==4, 'Windows 151 must select the App session-key and heartbeat contract')
assert(attempt('android',2,now,false)==1)
assert(attempt('ios',2,now,false)==1)
assert(attempt('win',1,now,false)==1, 'legacy PC (71) must remain valid')
assert(marked==2, 'legacy PC 71 retains its original session-key contract')
assert(attempt('unknown',2,now,false)==0)
assert(attempt('win',2,now,true)==0, 'bad signature must be rejected')
assert(result=='登陆验证失败2')
assert(attempt('win',2,now-301,false)==0, 'expired timestamp must be rejected')
assert(attempt('win',2,now+301,false)==0, 'future timestamp must be rejected')
assert(attempt('android',1,now,false)==0, 'legacy PC platform restriction remains')
print('Original login policy: Windows/Android/iOS, signatures and timestamp checks passed')
