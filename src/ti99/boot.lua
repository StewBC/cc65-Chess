-- skip the title screen and enter the cart.  _start is at >6020 in
-- header.asm (header + program list + CHSS marker).
local jumped = false
local frames = 0
emu.register_frame_done(function()
	if jumped then return end
	frames = frames + 1
	if frames < 8 then return end
	local cpu = manager.machine.devices[":maincpu"]
	cpu.state["WP"].value = 0x8300
	cpu.state["PC"].value = 0x6022
	jumped = true
end)
