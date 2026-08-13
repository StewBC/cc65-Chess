"""Minimal VICE binary monitor client (wire format per c64m/agents/vice-oracle.md).

Request : 02 <api=02> <u32 body_len> <u32 req_id> <u8 cmd> <body>
Response: 02 <api>    <u32 body_len> <u8 type> <u8 error> <u32 req_id> <body>

Responses arrive out of order and unsolicited, so match on request id and queue
the rest.  EXIT (0xaa) emits no id-matched reply -- fire and forget.
"""
import socket
import struct

CMD_MEM_GET = 0x01
CMD_CHECKPOINT_SET = 0x12
CMD_REGISTERS_GET = 0x31
CMD_ADVANCE_INSTR = 0x71
CMD_BANKS_AVAILABLE = 0x82
CMD_REGISTERS_AVAILABLE = 0x83
CMD_DISPLAY_GET = 0x84
CMD_EXIT = 0xAA
CMD_RESET = 0xCC

RESP_CHECKPOINT_INFO = 0x11
RESP_REGISTER_INFO = 0x31
RESP_STOPPED = 0x62
RESP_RESUMED = 0x63


class Vice:
    def __init__(self, port, host='127.0.0.1', timeout=15.0):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.id = 0
        self.queue = []
        self.regnames = {}

    def _recv(self, n):
        buf = b''
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                raise RuntimeError('vice closed the socket')
            buf += chunk
        return buf

    def read_response(self):
        hdr = self._recv(12)
        assert hdr[0] == 0x02, 'bad STX %r' % hdr[:1]
        body_len, rtype, err, rid = struct.unpack('<IBBI', hdr[2:12])
        body = self._recv(body_len) if body_len else b''
        return {'type': rtype, 'error': err, 'id': rid, 'body': body}

    def send(self, cmd, body=b''):
        self.id = (self.id + 1) & 0x7FFFFFFF
        rid = self.id
        self.s.sendall(bytes([0x02, 0x02]) + struct.pack('<II', len(body), rid)
                       + bytes([cmd]) + body)
        return rid

    def cmd(self, cmd, body=b''):
        rid = self.send(cmd, body)
        while True:
            r = self.read_response()
            if r['id'] == rid:
                return r
            self.queue.append(r)

    # ---- convenience ----------------------------------------------------
    def mem(self, start, end, memspace=0, bank=0, side_effects=0):
        body = struct.pack('<BHHBH', side_effects, start, end, memspace, bank)
        r = self.cmd(CMD_MEM_GET, body)
        if r['error']:
            raise RuntimeError('MEM_GET error %d' % r['error'])
        n = struct.unpack('<H', r['body'][:2])[0]
        return r['body'][2:2 + n]

    def reg_names(self):
        if self.regnames:
            return self.regnames
        r = self.cmd(CMD_REGISTERS_AVAILABLE, bytes([0]))
        b = r['body']
        count = struct.unpack('<H', b[:2])[0]
        off = 2
        for _ in range(count):
            size = b[off]
            rid = b[off + 1]
            nlen = b[off + 3]
            name = b[off + 4:off + 4 + nlen].decode('ascii', 'replace')
            self.regnames[rid] = name
            off += size + 1
        return self.regnames

    def parse_registers(self, body):
        names = self.reg_names()
        count = struct.unpack('<H', body[:2])[0]
        off = 2
        out = {}
        for _ in range(count):
            size = body[off]
            rid = body[off + 1]
            val = struct.unpack('<H', body[off + 2:off + 4])[0]
            out[names.get(rid, 'r%d' % rid)] = val
            off += size + 1
        return out

    def registers(self):
        r = self.cmd(CMD_REGISTERS_GET, bytes([0]))
        return self.parse_registers(r['body'])

    def banks(self):
        r = self.cmd(CMD_BANKS_AVAILABLE)
        b = r['body']
        count = struct.unpack('<H', b[:2])[0]
        off = 2
        out = {}
        for _ in range(count):
            size = b[off]
            bid = struct.unpack('<H', b[off + 1:off + 3])[0]
            nlen = b[off + 3]
            out[b[off + 4:off + 4 + nlen].decode('ascii', 'replace')] = bid
            off += size + 1
        return out

    def checkpoint(self, start, end=None, op=4, stop=1, enabled=1, temporary=0):
        end = start if end is None else end
        body = struct.pack('<HHBBBB', start, end, stop, enabled, op, temporary)
        return self.cmd(CMD_CHECKPOINT_SET, body)

    def cont(self):
        """EXIT resumes; it emits no id-matched reply."""
        self.send(CMD_EXIT)

    def wait_stopped(self, limit=4000):
        """Read the async stream to STOPPED, capturing REGISTER_INFO on the way."""
        regs = None
        hit = None
        for _ in range(limit):
            r = self.read_response()
            if r['type'] == RESP_REGISTER_INFO:
                regs = self.parse_registers(r['body'])
            elif r['type'] == RESP_CHECKPOINT_INFO:
                hit = r['body']
            elif r['type'] == RESP_STOPPED:
                pc = struct.unpack('<H', r['body'][:2])[0] if r['body'] else None
                return {'pc': pc, 'regs': regs, 'checkpoint': hit}
        raise RuntimeError('no STOPPED')

    def step(self, n=1, step_over=0):
        return self.cmd(CMD_ADVANCE_INSTR, struct.pack('<BH', step_over, n))

    def close(self):
        self.s.close()


CMD_MEM_SET = 0x02


def mem_set(v, start, data, memspace=0, bank=0, side_effects=0):
    end = start + len(data) - 1
    body = struct.pack('<BHHBH', side_effects, start, end, memspace, bank) + data
    r = v.cmd(CMD_MEM_SET, body)
    if r['error']:
        raise RuntimeError('MEM_SET error %d' % r['error'])
    return r


CMD_REGISTERS_SET = 0x32


def regs_set(v, **kw):
    """regs_set(v, PC=0x100D) -- ids come from REGISTERS_AVAILABLE."""
    ids = {name: rid for rid, name in v.reg_names().items()}
    items = b''
    for name, val in kw.items():
        items += struct.pack('<BBH', 3, ids[name], val)
    body = bytes([0]) + struct.pack('<H', len(kw)) + items
    r = v.cmd(CMD_REGISTERS_SET, body)
    if r['error']:
        raise RuntimeError('REGISTERS_SET error %d' % r['error'])
    return v.parse_registers(r['body'])
