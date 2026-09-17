-----------------------------------------
--                                     --
-- @name:     Startup script           --
-- @author:   SWAT                   	--
-- @url:      http://www.dc-swat.ru    --
--                                     --
-----------------------------------------
--
-- Internal DreamShell lua functions:
-- 
--	OpenModule             Open module file and return ID
--	CloseModule            Close module by ID
--	GetModuleByName        Get module ID by module NAME
--
--	AddApp                 Add app by XML file, return app NAME
--	OpenApp                Open app by NAME (second argument for args)
--	CloseApp               Close app by NAME (second argument for change unload flag)
--
--	ShowConsole
--	HideConsole
--	SetDebugIO             Set the debug output (scif, dclsocket, fb, ds, sd). By default is ds.
--	Sleep                  Sleep in current thread (in ms)
--	MapleAttached          Check for attached maple device
--
--	Bit library:           bit.or, bit.and, bit.not, bit.xor
--	File system library:   lfs.chdir, lfs.currentdir, lfs.dir, lfs.mkdir, lfs.rmdir,
--                         lfs.copyfile, lfs.rename
--	
------------------------------------------

-- Keep one small log per boot. Flush each step so a native load failure still
-- leaves its last completed step on writable media. Logging must not stop boot.
local startup_log;
local base_path = os.getenv("PATH");
if base_path then
	local ok, file = pcall(io.open, base_path .. "/kui-startup.log", "w");
	if ok then startup_log = file; end
end

local function startup_message(message)
	print(message);
	if startup_log then
		pcall(function()
			startup_log:write(message);
			startup_log:flush();
		end);
	end
end

local DreamShell = {

	initialized = false,
	
	modules = {
		-- Lua keeps binding callbacks and userdata metatables after apps close.
		-- Keep one session reference so app unload cannot free their code.
		"tolua",
		--"tolua_2plus",
		"luaDS",            -- Depends: tolua
		--"luaKOS",           -- Depends: tolua
		"luaSDL",           -- Depends: tolua
		"luaGUI",           -- Depends: tolua
		--"luaMXML",          -- Depends: tolua
		--"luaSTD",           -- Depends: tolua
		--"sqlite3",
		--"luaSQL",           -- Depends: sqlite3
		--"luaSocket",
		--"luaTask",
		--"bzip2",
		--"minilzo",
		--"zip",              -- Depends: bzip2
		--"http",
		--"httpd",
		--"telnetd",
		--"ftpd",
		--"mongoose",
		--"ppp",
		--"mpg123",
		--"oggvorbis",
		--"adx",
		--"s3m",
		--"wave",
		--"xvid",
		--"SDL_mixer",        -- Depends: oggvorbis
		--"ffmpeg",
		--"opengl",
		--"isofs",            -- Depends: minilzo
		--"isoldr",           -- Depends: isofs
		--"SDL_net",
		--"opkg",             -- Depends: minilzo
		--"aicaos",
		--"gumbo",
		--"ini",
		--"bflash",
		--"openssl",
		--"bitcoin",
		--"quirc",
		--"polarssl",
		--"mbedtls",
		--"curl"              -- Depends: mbedtls
	},

	Initialize = function(self)

		os.execute("env USER Default");
		local path = os.getenv("PATH");
		local time = os.time();

		startup_message(os.getenv("HOST") .. " " .. os.getenv("VERSION") .. "\n");
		startup_message(os.getenv("ARCH") .. ": " .. os.getenv("BOARD_ID") .. "\n");
		startup_message("Date: " .. os.date() .. "\n");
		startup_message("Base path: " .. path .. "\n");
		startup_message("User: " .. os.getenv("USER") .. "\n");

		local emu = os.getenv("EMU");

		if emu ~= nil then
			startup_message("Emulator: " .. emu .. "\n");
		end

		startup_message("\n");

		if not MapleAttached("Keyboard") then
			table.insert(self.modules, "vkb");
		end

		table.foreach(self.modules, function(k, name)  
			startup_message("DS_PROCESS: Loading module " .. name .. "...\n");
			if not OpenModule(path .. "/modules/" .. name .. ".klf") then
				startup_message("DS_ERROR: Can't load module " .. path .. "/modules/" .. name .. ".klf\n");
			end
		end);

		-- RTC validity check and fix.
		if time < 946684800 or time > 3471292800 then
			os.execute("rtc --set --unix 1735689600");
		end

		self:InstallingApps(path .. "/apps");
		local startup_app = os.getenv("STARTUP_APP");
		local default_app = "Launch App";
		if not startup_app or startup_app == "" then startup_app = default_app; end
		startup_message("K-UI: Opening startup app: " .. startup_app .. "\n");
		local opened = OpenApp(startup_app);
		if not opened and startup_app ~= default_app then
			startup_message("K-UI: Startup app unavailable; opening " .. default_app .. " instead.\n");
			opened = OpenApp(default_app);
		end
		if not opened then
			error("K-UI could not open the launcher. Check the module and app messages above.");
		end
		self.initialized = true;
		startup_message("K-UI: Startup app opened.\n");

		local startup_cmd = os.getenv("STARTUP_CMD");
		if startup_cmd ~= nil and startup_cmd ~= "" then
			os.execute(startup_cmd);
		end
	end,

	InstallingApps = function(self, path)

		startup_message("DS_PROCESS: Installing apps...\n");
		local name = nil;
		local list = {};

		for ent in lfs.dir(path) do
			if ent ~= nil and ent.name ~= ".." and ent.name ~= "." and ent.name ~= "main" and ent.attr ~= 0 then
				table.insert(list, ent.name);
			end
		end

		table.sort(list, function(a, b) return a > b end);

		for index, directory in ipairs(list) do

			name = AddApp(path .. "/" .. directory .. "/app.xml");

			if not name then
				startup_message("DS_ERROR: " .. directory .. "\n");
			else
				startup_message("DS_OK: " .. name .. "\n");
			end
		end

		return true;
	end
};

startup_message("K-UI startup recovery log\n");
local ok, failure = xpcall(function()
	DreamShell:Initialize();
end, function(message)
	local detail = tostring(message);
	if debug and debug.traceback then detail = debug.traceback(detail, 2); end
	startup_message("K-UI STARTUP FAILED: " .. detail .. "\n");
	ShowConsole();
	return detail;
end);
if startup_log then pcall(function() startup_log:close(); end); end
if not ok then error(failure, 0); end
