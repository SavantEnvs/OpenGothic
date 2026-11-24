#!/usr/bin/env python3
# Writes the fuzz_dmusic_segment seeds: one RIFF file per seed that is a segment (segh, tempo/style/
# band/command/chord tracks), a style (styh, part with notes and curves, patterns, band) and a DLS
# collection (instruments, regions, articulators, pool table, wave pool) at once, since the harness
# resolver returns the same bytes for every name. Usage: gen_seeds.py <outdir>
import struct, sys, os, math
def ck(id, data): 
    d = id.encode() + struct.pack('<I', len(data)) + data
    return d + (b'\0' if len(data) % 2 else b'')
def lst(typ, *items, id='LIST'): return ck(id, typ.encode() + b''.join(items))
def riff(typ, *items): return lst(typ, *items, id='RIFF')
def u16s(s): return s.encode('utf-16-le') + b'\0\0'
G_PART = bytes(range(0x10, 0x20)); G_STY = bytes(range(0x20, 0x30)); G_DLS = bytes(range(0x30, 0x40))
def ref(file, guid, name):
    return lst('DMRF', ck('refh', b'\0'*16 + struct.pack('<I', 0x13)), ck('guid', guid), ck('name', u16s(name)), ck('file', u16s(file)))
def trk(*body): return riff('DMTK', ck('trkh', b'\0'*16 + struct.pack('<IIII', 0, 1, 0, 0)), *body)
def band(patches, dls_flags=1):
    ins = []
    for ch, p in enumerate(patches):
        bins = struct.pack('<II4III', p, 0, 0, 0, 0, 0, ch, dls_flags | (1<<5) | (1<<6)) + struct.pack('<BBhI', 64, 100, 0, 0)
        ins.append(lst('lbin', ck('bins', bins), ref('music.dls', G_DLS, 'Instr')))
    return riff('DMBD', ck('guid', bytes(16)), lst('UNFO', ck('UNAM', u16s('band'))), lst('lbil', *ins))
def segment(cmds, chords=True):
    tempo = ck('tetr', struct.pack('<I', 16) + struct.pack('<IId', 0, 0, 120.0) + struct.pack('<IId', 1536, 0, 90.0))
    cmd = ck('cmnd', struct.pack('<I', 12) + b''.join(struct.pack('<IHBBBBBx', t, m, 0, c, 50, 10, 0) for t, m, c in cmds))
    crdb = struct.pack('<I', 40) + u16s('C').ljust(32, b'\0') + struct.pack('<IHBB', 0, 0, 0, 0) + struct.pack('<II', 2, 20) \
        + struct.pack('<IIIIBBxx', 0x91, 0xAB5AB5, 0xFFFFFF, 0xF, 0, 0) + struct.pack('<IIIIBBxx', 0x891, 0xAB5AB5, 0xFFFFFF, 0x2, 4, 0)
    cord = lst('cord', ck('crdh', struct.pack('<I', 0)), ck('crdb', crdb))
    sttr = lst('sttr', lst('strf', ck('stmp', struct.pack('<I', 0)), ref('music.sty', G_STY, 'Style')))
    bnd = riff('DMBT', lst('lbdl', lst('lbnd', ck('bdih', struct.pack('<I', 0)), band([0, 0x0101]))))
    tracks = [trk(tempo), trk(sttr), trk(bnd), trk(cmd)] + ([trk(cord)] if chords else [])
    return [ck('segh', struct.pack('<IIIIII', 1, 768*4*2, 0, 0, 0, 0)), ck('guid', G_STY),
            lst('UNFO', ck('UNAM', u16s('seg'))), lst('trkl', *tracks)]
def style(npatterns=2):
    prth = struct.pack('<BBH', 4, 4, 4) + struct.pack('<32I', *([0x0FFFFFFF, 0x3] + [0]*30)) + G_PART + struct.pack('<HBBBB', 2, 2, 127, 0, 0)
    notes = [(0, 1, 768, 0, 60, 100, 0, 0, 0, 0, 0), (4, 1, 384, 0, 0x0102, 90, 0, 0, 0, 0, 16),
             (8, 3, 384, 10, 0x0001, 80, 195, 2, 4, 0, 8), (12, 2, 384, 0, 0x0003, 80, 0, 0, 0, 0, 4)]
    note = ck('note', struct.pack('<I', 22) + b''.join(struct.pack('<IIIhHBBBBBB', *n) for n in notes))
    curves = [(0, 1, 768, 0, 0, 0, 127, 0, 3, 0, 7, 0), (2, 1, 768, 0, 0, 0, 8191, 0, 1, 1, 0, 0), (6, 1, 768, 0, 0, 127, 0, 0, 3, 2, 10, 0)]
    crve = ck('crve', struct.pack('<I', 28) + b''.join(struct.pack('<IIIIhhhhBBBB', *c) for c in curves))
    part = lst('part', ck('prth', prth), lst('UNFO', ck('UNAM', u16s('part'))), note, crve)
    pats = []
    for i in range(npatterns):
        emb = [0, 1, 2, 4][i % 4]
        ptnh = struct.pack('<BBH', 4, 4, 4) + struct.pack('<BBHH', 1, 100, emb, 2) + b'\0\0'
        prfc = G_PART + struct.pack('<HBBBB', 0, 0, 0, 0, 0)
        pats.append(lst('pttn', ck('ptnh', ptnh), ck('rhtm', struct.pack('<2I', 1, 1)), lst('pref', ck('prfc', prfc))))
    return [ck('styh', struct.pack('<BBHd', 4, 4, 4, 120.0)), part] + pats + [band([0])]
def dls(adpcm=False, loop=True):
    wsmp = struct.pack('<IHhiII', 20, 60, 0, 0, 0, 1 if loop else 0) + (struct.pack('<IIII', 16, 0, 10, 100) if loop else b'')
    def rgn(lo, hi): return lst('rgn ', ck('rgnh', struct.pack('<HHHHHH', lo, hi, 0, 127, 0, 0)), ck('wsmp', wsmp), ck('wlnk', struct.pack('<HHII', 0, 0, 1, 0)))
    art = lst('lart', ck('art1', struct.pack('<II', 8, 2) + struct.pack('<HHHHi', 0, 0, 0x0206, 0, -1000 << 16) + struct.pack('<HHHHi', 0, 0, 0x0001, 0, 1 << 16)))
    ins = [lst('ins ', ck('insh', struct.pack('<III', 2, 0, 0)), ck('dlid', bytes(16)), lst('lrgn', rgn(0, 63), rgn(64, 127)), art, lst('INFO', ck('INAM', b'Inst\0'))),
           lst('ins ', ck('insh', struct.pack('<III', 1, 1, 1)), lst('lrgn', rgn(0, 127)), lst('INFO', ck('INAM', b'Bass\0')))]
    pcm = b''.join(struct.pack('<h', int(8000*math.sin(i/4))) for i in range(200))
    if adpcm:
        fmt = struct.pack('<HHIIHHHH', 2, 1, 22050, 11100, 64, 4, 32, 116) + struct.pack('<H', 7) + b''.join(struct.pack('<hh', a, b) for a, b in [(256,0),(512,-256),(0,0),(192,64),(240,0),(460,-208),(392,-232)])
        data = (struct.pack('<BhhH', 0, 16, 0, 0) + bytes(57))[:64] * 2
    else:
        fmt = struct.pack('<HHIIHH', 1, 1, 22050, 44100, 2, 16) + b'\0\0'
        data = pcm
    wave = lst('wave', ck('fmt ', fmt), ck('wsmp', wsmp), ck('data', data), lst('INFO', ck('INAM', b'w\0')))
    return [ck('colh', struct.pack('<I', 2)), ck('vers', struct.pack('<II', 1, 0)), ck('dlid', G_DLS),
            lst('lins', *ins), ck('ptbl', struct.pack('<II', 8, 1) + struct.pack('<I', 0)), lst('wvpl', wave),
            lst('INFO', ck('INAM', b'music\0'), ck('ICOP', b'none\0'))]
out = sys.argv[1]
# trailing byte (outside the RIFF): the harness's transition control, embellishment = c % 7, timing = 1 + (c / 7) % 4
variants = {
 'polyglot_groove.sgt': (segment([(0, 0, 0), (3072, 2, 1)]) + style() + dls(), 2),
 'polyglot_embellish.sgt': (segment([(0, 0, 2), (1536, 1, 1), (3072, 2, 4)]) + style(4) + dls(loop=False), 4 + 7 * 3),
 'polyglot_adpcm.sgt': (segment([(0, 0, 0)], chords=False) + style(1) + dls(adpcm=True), 5 + 7 * 2),
 'polyglot_intro.sgt': (segment([(0, 0, 0), (1536, 1, 2)]) + style(3) + dls(), 3 + 7 * 1),
}
for name, (items, ctl) in variants.items():
    open(os.path.join(out, name), 'wb').write(riff('DMSG', *items) + bytes([ctl]))
