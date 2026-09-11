# print DECB load/exec from a CoCo .bin
import struct, sys, pathlib

p = pathlib.Path(sys.argv[1])
d = p.read_bytes()
i = 0
load = end = 0
while i < len(d):
	pre = d[i]
	if pre == 0:
		ln, ad = struct.unpack(">HH", d[i + 1:i + 5])
		if not load:
			load = ad
		end = ad + ln
		i += 5 + ln
	elif pre == 0xFF:
		ex = struct.unpack(">HH", d[i + 1:i + 5])[1]
		print("coco3: load $%04x-$%04x exec $%04x size %d  headroom %d" % (
			load, end, ex, len(d), 0x7E00 - end))
		break
	else:
		sys.exit("bad DECB preamble %02X" % pre)
