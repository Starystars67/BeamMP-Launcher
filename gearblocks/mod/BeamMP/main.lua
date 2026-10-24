-- BeamMP for GearBlocks.
-- Talks to the BeamMP launcher through the BeamMP plugin (the `BeamMP` table) and shows the room the launcher is in.
-- Messages from the launcher are JSON lines: {"type":"session","session":{state, code, role, peers, ...}}.

print('BeamMP: starting')

if BeamMP == nil then
	print('BeamMP: the BeamMP plugin is not installed')
end

local VERSION = '0.1.0'
local RETRY_TICKS = 100 -- about two seconds at the fixed update rate

----- Window -----

local win = Windows.CreateWindow()
win.SetAlignment( align_RightEdge, 20, 240 )
win.SetAlignment( align_TopEdge, 80, 200 )
win.Title = 'BeamMP'
win.OnClose.add( function() UnloadScript.Raise( ScriptName ) end )
win.Show( true )

local label = win.CreateLabel()
label.SetAlignment( align_HorizEdges, 10, 10 )
label.SetAlignment( align_VertEdges, 10, 10 )
label.FontSize = 10
label.Alignment = textAnc_MiddleLeft

----- Text -----

-- Names come from other players and the label shows rich text: a name must not be able to add tags.
local function plain( text )
	return ( tostring( text ):gsub( '<', '\226\128\185' ) )
end

local function describe( session )
	if session == nil or session.state == nil or session.state == 'idle' then
		local why = session and session.error and session.error.message
		if why then
			return 'No room.\n' .. plain( why )
		end
		return 'No room.\nHost or join one in the BeamMP launcher.'
	end
	if session.state == 'connecting' then
		return 'Connecting to the relay...'
	end
	local lines = { 'Room <b>' .. plain( session.code or '?' ) .. '</b>' }
	if session.role == 'host' then
		lines[#lines + 1] = 'You are the host.'
	else
		lines[#lines + 1] = 'You joined this room.'
	end
	local peers = session.peers or {}
	lines[#lines + 1] = 'Players (' .. #peers .. '):'
	for _, peer in ipairs( peers ) do
		local mark = ''
		if peer.id == session.host_id then mark = mark .. ' (host)' end
		if peer.id == session.self_id then mark = mark .. ' (you)' end
		lines[#lines + 1] = '  ' .. plain( peer.name ) .. mark
	end
	return table.concat( lines, '\n' )
end

local session = nil
local shown = nil

local function show( text )
	if text ~= shown then
		shown = text
		label.Text = text
	end
end

----- Launcher connection -----

local greeted = false
local ticks = 0

local function handle( line )
	local ok, message = pcall( json.parse, line )
	if not ok or type( message ) ~= 'table' then
		return
	end
	if message.type == 'session' then
		session = message.session
		print( 'BeamMP: session ' .. tostring( session and session.state ) )
	end
end

----- Entry functions -----

function FixedUpdate()
	if BeamMP == nil then
		show( 'The BeamMP plugin is not installed.' )
		return
	end

	ticks = ticks + 1
	if not BeamMP.connected() then
		greeted = false
		session = nil
		if ticks % RETRY_TICKS == 1 then
			BeamMP.connect()
		end
		show( 'Waiting for the BeamMP launcher...' )
		return
	end

	if not greeted then
		greeted = BeamMP.send( json.serialize( { type = 'hello', mod = 'beammp', version = VERSION } ) )
	end

	local line = BeamMP.poll()
	while line do
		handle( line )
		line = BeamMP.poll()
	end
	show( describe( session ) )
end

function Cleanup()
	Windows.DestroyWindow( win )
end
