-- Execute the real resource's initialization, not a duplicated implementation.
dofile("runtime/gmsv/data/ablua/capturefunction.lua")
main()
assert(type(rarepetid1) == "table")
assert(maxrarepetid1 == 0, "the original rare-pet configuration is disabled")
assert(#NeedEnemy > 0, "ordinary capture requirements must remain loaded")
assert(#enemylist > 0 and #ranktbl == 6)
print("Original capture script initialized with disabled rare-pet table")
