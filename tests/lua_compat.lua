-- The shipped gameplay scripts use Lua 5.1 table.getn on sequences.
assert(type(table.getn) == "function", "legacy gameplay requires table.getn")
assert(table.getn({}) == 0)
assert(table.getn({false, 10, "three"}) == 3)
local values = {"a", "b", "c"}
table.remove(values)
assert(table.getn(values) == 2)
assert(not pcall(table.getn, "not a table"))
print("Lua gameplay compatibility passed")
