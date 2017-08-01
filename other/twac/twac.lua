TWAC = {
	basepath = PathDir(ModuleFilename()),
	
	OptFind = function (name, required)	
		local check = function(option, settings)
			option.value = false
			option.use_libver = 0
			option.lib_path = nil
			if platform == "win32" or arch == "ia32" then
				option.value = true
				option.use_libver = 32
			elseif platform == "win64" or arch == "amd64" then
				option.value = true
				option.use_libver = 64
			end
		end
		
		local apply = function(option, settings)
			if option.value == true then
				-- include path
				settings.cc.includes:Add(TWAC.basepath .. "/include")
				
				if option.use_libver == 32 then
					settings.link.libpath:Add(TWAC.basepath .. "/lib32")
				else
					settings.link.libpath:Add(TWAC.basepath .. "/lib64")
				end
				settings.link.libs:Add("TWAntiCheats")
			end
		end
		
		local save = function(option, output)
			output:option(option, "value")
			output:option(option, "use_libver")
		end
		
		local display = function(option)
			if option.value == true then
				if option.use_libver == 32 then return "using supplied 32bits libraries" end
				if option.use_libver == 64 then return "using supplied 64bits libraries" end
				return "using unknown method"
			else
				if option.required then
					return "not found (required)"
				else
					return "not found (optional)"
				end
			end
		end
		
		local o = MakeOption(name, 0, check, save, display)
		o.Apply = apply
		o.include_path = nil
		o.lib_path = nil
		o.required = required
		return o
	end
}
