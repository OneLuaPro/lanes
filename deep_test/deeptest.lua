local lanes = require("lanes").configure{ with_timers = false}
local l = lanes.linda "my linda"

-- we will transfer userdata created by this module, so we need to make Lanes aware of it
local dt = lanes.require "deep_test"

-- set DEEP to any non-false value to run the Deep Userdata tests. "gc" selects a special test for debug purposes
DEEP = DEEP or true
-- set CLONABLE to any non-false value to run the Clonable Userdata tests
CLONABLE = CLONABLE or true

-- lua 5.1->5.2 support a single table uservalue
-- lua 5.3->5.4 supports an arbitrary type uservalue
local test_uvtype = (_VERSION == "Lua 5.4") and "function" or (_VERSION == "Lua 5.3") and "string" or "table"
-- lua 5.4 supports multiple uservalues
local nupvals = _VERSION == "Lua 5.4" and 3 or 1

local makeUserValue = function( obj_)
	if test_uvtype == "table" then
		return {"some uservalue"}
	elseif test_uvtype == "string" then
		return "some uservalue"
	elseif test_uvtype == "function" then
		-- a function that pull the userdata as upvalue
		local f = function()
			return "-> '" .. tostring( obj_) .. "'"
		end
		return f
	end
end

local printDeep = function( prefix_, obj_, t_)
	print( prefix_, obj_)
	for uvi = 1, nupvals do
		local uservalue = obj_:getuv(uvi)
		print ("uv #" .. uvi, type( uservalue), uservalue, type(uservalue) == "function" and uservalue() or "")
	end
	if t_ then
		for k, v in pairs( t_) do
			print( "t["..tostring(k).."]", v)
		end
	end
	print()
end

local performTest = function( obj_)
	-- setup the userdata with some value and a uservalue
	obj_:set( 666)
	obj_:setuv( 1, makeUserValue( obj_))
	if nupvals > 1 then
		-- keep uv #2 as nil
		obj_:setuv( 3, "ENDUV")
	end

	local t =
	{
		["key"] = obj_,
		[obj_] = "val" -- this one won't transfer because we don't support full uservalue as keys
	}

	-- read back the contents of the object
	printDeep( "immediate:", obj_, t)

	-- send the object in a linda, get it back out, read the contents
	l:set( "key", obj_, t)
	-- when obj_ is a deep userdata, out is the same userdata as obj_ (not another one pointing on the same deep memory block) because of an internal cache table [deep*] -> proxy)
	-- when obj_ is a clonable userdata, we get a different clone everytime we cross a linda or lane barrier
	printDeep( "out of linda:", l:get( "key", 2))

	-- send the object in a lane through parameter passing, the lane body returns it as return value, read the contents
	local g = lanes.gen(
		"package"
		, {
			required = { "deep_test"} -- we will transfer userdata created by this module, so we need to make this lane aware of it
		}
		, function( arg_, t_)
			-- read contents inside lane: arg_ and t_ by argument
			printDeep( "in lane, as arguments:", arg_, t_)
			-- read contents inside lane: obj_ and t by upvalue
			printDeep( "in lane, as upvalues:", obj_, t)
			-- read contents inside lane: in linda
			printDeep( "in lane, from linda:", l:get("key", 2))
			return arg_, t_
		end
	)
	h = g( obj_, t)
	-- when obj_ is a deep userdata, from_lane is the same userdata as obj_ (not another one pointing on the same deep memory block) because of an internal cache table [deep*] -> proxy)
	-- when obj_ is a clonable userdata, we get a different clone everytime we cross a linda or lane barrier
	printDeep( "from lane:", h[1], h[2])
end

if DEEP then
	print "================================================================"
	print "DEEP"
	local d = dt.new_deep(nupvals)
	if DEEP == "gc" then
		local thrasher = function(repeat_, size_)
			print "in thrasher"
			-- result is a table of repeat_ tables, each containing size_ entries
			local result = {}
			for i = 1, repeat_ do
				local batch_values = {}
				for j = 1, size_ do
					table.insert(batch_values, j)
				end
				table.insert(result, batch_values)
			end
			print "thrasher done"
			return result
		end
		-- have the object call the function from inside one of its functions, to detect if it gets collected from there (while in use!)
		local r = d:invoke(thrasher, REPEAT or 10, SIZE or 10)
		print("invoke -> ", tostring(r))
	else
		performTest(d)
	end
end

if CLONABLE then
	print "================================================================"
	print "CLONABLE"
	performTest( dt.new_clonable(nupvals))
end

print "================================================================"
print "TEST OK"