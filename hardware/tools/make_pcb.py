"""Generate the carrier PCB with KiCad's python (pcbnew) and a small grid router.
Run with: /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 tools/make_pcb.py out.kicad_pcb
"""
import sys, os, heapq, math
from array import array
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
from pcbnew import VECTOR2I, FromMM, ToMM
from design import *

FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/"
BW, BH = 72.0, 86.0          # board size, mm
G = 0.127                     # router grid, mm
CLR = 0.2
board = pcbnew.BOARD()
nets = {}
def net(name):
    if name not in nets:
        ni = pcbnew.NETINFO_ITEM(board, name); board.Add(ni); nets[name] = ni
    return nets[name]
def mm(x, y): return VECTOR2I(FromMM(x), FromMM(y))

def add_fp(lib, name, ref, value, x, y, angle=0):
    fp = pcbnew.FootprintLoad(FP + lib + ".pretty", name)
    fp.SetReference(ref); fp.SetValue(value)
    fp.SetPosition(mm(x, y)); fp.SetOrientationDegrees(angle)
    board.Add(fp); return fp

def row(lib, name, ref, value, x, y, n, pinnets):
    """Horizontal header, pin 1 at left."""
    for ang in (90, 270):
        fp = add_fp(lib, name, ref, value, x, y, ang)
        p = {pd.GetNumber(): pd.GetPosition() for pd in fp.Pads()}
        if p["2"].x > p["1"].x and abs(p["2"].y - p["1"].y) < 10: break
        board.Remove(fp)
    for pd in fp.Pads(): 
        nm = pinnets.get(pd.GetNumber())
        if nm: pd.SetNet(net(nm))
    return fp

PSK = ("Connector_PinSocket_2.54mm", "PinSocket_1x%02d_P2.54mm_Vertical")
def nets_of(pins): return {str(i+1): (n if n else None) for i,(_,n) in enumerate(pins)}
def pname(n): return n if n in ("GND","+3V3","+5V") else "/" + n

def mapnets(d): return {k: (pname(v) if v else None) for k, v in d.items()}

X0 = 12.7
YT, YB = 25.4, 45.72     # 12-pin row (top), 16-pin row (bottom), 0.8 in apart
f16 = {str(i+1): FEATHER_NETS.get(n) for i, n in enumerate(FEATHER_16)}
f12 = {str(i+1): FEATHER_NETS.get(n) for i, n in enumerate(FEATHER_12)}
parts = {}
parts["J1"] = row(PSK[0], PSK[1] % 16, "J1", "Feather 16-pin", X0, YB, 16, mapnets(f16))
parts["J2"] = row(PSK[0], PSK[1] % 12, "J2", "Feather 12-pin", X0 + 4*2.54, YT, 12, mapnets(f12))
parts["J3"] = row(PSK[0], PSK[1] % 8, "J3", "microSD breakout", 30.48, 60.96, 8, mapnets(nets_of(SD_PINS)))
parts["J4"] = row(PSK[0], PSK[1] % 6, "J4", "I2S mic breakout", 12.7, 10.16, 6, mapnets(nets_of(MIC_PINS)))
parts["J5"] = row(PSK[0], PSK[1] % 7, "J5", "I2S amp breakout", 33.02, 10.16, 7, mapnets(nets_of(AMP_PINS)))
parts["JP1"] = row("Connector_PinHeader_2.54mm", "PinHeader_1x03_P2.54mm_Vertical", "JP1", "Amp power select", 53.34, 15.24, 3, mapnets(nets_of(JP_PINS)))
c1 = add_fp("Capacitor_THT", "CP_Radial_D5.0mm_P2.00mm", "C1", "10uF", 63.5, 15.24, 0); parts["C1"] = c1
for pd in c1.Pads(): pd.SetNet(net("/AMP_VIN" if pd.GetNumber()=="1" else "GND"))
r1 = add_fp("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal", "R1", "330R", 50.8, 30.48, 0); parts["R1"] = r1
for pd in r1.Pads(): pd.SetNet(net("/LED_DRV" if pd.GetNumber()=="1" else "/LED_A"))
j6 = add_fp("Connector_PinHeader_2.54mm", "PinHeader_1x02_P2.54mm_Vertical", "J6", "External LED", 66.04, 27.94, 0); parts["J6"] = j6
for pd in j6.Pads(): pd.SetNet(net("/LED_A" if pd.GetNumber()=="1" else "GND"))
parts["J7"] = row(PSK[0], PSK[1] % 4, "J7", "20x4 I2C LCD", 12.7, 75.0, 4, mapnets(nets_of(LCD_PINS)))
parts["J8"] = row("Connector_PinHeader_2.54mm", "PinHeader_1x03_P2.54mm_Vertical", "J8", "Mode toggle", 33.02, 75.0, 3, mapnets(nets_of(MODE_PINS)))
parts["J9"] = row("Connector_PinHeader_2.54mm", "PinHeader_1x02_P2.54mm_Vertical", "J9", "Rotary phone dial", 53.34, 75.0, 2, mapnets({"1":"DIAL","2":"GND"}))
for i, (x, y) in enumerate([(4, 4), (68, 4), (4, 82), (68, 82)]):
    add_fp("MountingHole", "MountingHole_2.7mm_M2.5", "H%d" % (i+1), "M2.5", x, y)

# ---- board outline ------------------------------------------------------
def edge(x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(mm(x1, y1)); s.SetEnd(mm(x2, y2)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(FromMM(0.1)); board.Add(s)
for a, b in [((0,0),(BW,0)),((BW,0),(BW,BH)),((BW,BH),(0,BH)),((0,BH),(0,0))]: edge(*a, *b)

# ---- router -------------------------------------------------------------
NX, NY = int(BW/G) + 1, int(BH/G) + 1
own = [array('h', [0]) * (NX*NY) for _ in range(2)]
def cell(x, y): return int(round(x/G)), int(round(y/G))
def idx(ix, iy): return iy*NX + ix
netid = {n: i+1 for i, n in enumerate(sorted(nets))}
MARGIN = CLR + 0.15
def paint_rect(layer, cx, cy, hx, hy, k):
    x0, x1 = int((cx-hx)/G), int((cx+hx)/G)+1; y0, y1 = int((cy-hy)/G), int((cy+hy)/G)+1
    a = own[layer]
    for iy in range(max(y0,0), min(y1,NY-1)+1):
        for ix in range(max(x0,0), min(x1,NX-1)+1):
            j = idx(ix, iy); o = a[j]
            if o == 0: a[j] = k
            elif o != k: a[j] = -2
DISK = [(dx, dy) for dx in range(-4, 5) for dy in range(-4, 5) if dx*dx+dy*dy <= 16]
def paint_cell(layer, ix, iy, k, disk=DISK):
    a = own[layer]
    for dx, dy in disk:
        x, y = ix+dx, iy+dy
        if 0 <= x < NX and 0 <= y < NY:
            j = idx(x, y); o = a[j]
            if o == 0: a[j] = k
            elif o != k: a[j] = -2
# pads
terms = {}     # net name -> list of (ref, pad number, x, y)
for ref, fp in list(parts.items()):
    for pd in fp.Pads():
        bb = pd.GetBoundingBox(); cx = ToMM(bb.GetCenter().x); cy = ToMM(bb.GetCenter().y)
        hx = ToMM(bb.GetWidth())/2 + MARGIN; hy = ToMM(bb.GetHeight())/2 + MARGIN
        nm = pd.GetNetname()
        k = netid.get(nm, -1)
        for L in (0, 1): paint_rect(L, cx, cy, hx, hy, k if k > 0 else -1)
        if nm and nm != "GND":
            p = pd.GetPosition(); terms.setdefault(nm, []).append((ref, pd.GetNumber(), ToMM(p.x), ToMM(p.y)))
for (x, y) in [(4, 4), (68, 4), (4, 82), (68, 82)]:
    for L in (0, 1): paint_rect(L, x, y, 1.35+MARGIN, 1.35+MARGIN, -1)
for L in (0, 1):                      # board edge keep-out
    e = int(0.8/G)
    for iy in range(NY):
        for ix in range(NX):
            if ix < e or iy < e or ix > NX-1-e or iy > NY-1-e: own[L][idx(ix, iy)] = -1

NB = [(1,0,1.0),(-1,0,1.0),(0,1,1.0),(0,-1,1.0),(1,1,1.4142),(1,-1,1.4142),(-1,1,1.4142),(-1,-1,1.4142)]
VIA_COST = 12
VR = [(dx, dy) for dx in range(-3, 4) for dy in range(-3, 4) if dx*dx+dy*dy <= 9]
def passable(L, ix, iy, k):
    if not (0 <= ix < NX and 0 <= iy < NY): return False
    o = own[L][idx(ix, iy)]; return o == 0 or o == k
def via_ok(ix, iy, k):
    for dx, dy in VR:
        for L in (0, 1):
            if not passable(L, ix+dx, iy+dy, k): return False
    return True

def route(k, sources, target):
    """sources: set of (L,ix,iy); target: (ix,iy). Returns list of (L,ix,iy) path or None."""
    tx, ty = target
    def h(ix, iy):
        dx, dy = abs(ix-tx), abs(iy-ty); return (max(dx,dy) + 0.4142*min(dx,dy))
    dist = {}; prev = {}; pq = []
    for s in sources:
        dist[s] = 0; heapq.heappush(pq, (h(s[1], s[2]), 0, s))
    while pq:
        f, d, cur = heapq.heappop(pq)
        if d > dist.get(cur, 1e18): continue
        L, ix, iy = cur
        if (ix, iy) == (tx, ty):
            path = [cur]
            while cur in prev: cur = prev[cur]; path.append(cur)
            return path[::-1]
        for dx, dy, c in NB:
            nx, ny = ix+dx, iy+dy
            if not passable(L, nx, ny, k): continue
            if dx and dy and not (passable(L, ix+dx, iy, k) and passable(L, ix, iy+dy, k)): continue
            nd = d + c
            st = (L, nx, ny)
            if nd < dist.get(st, 1e18):
                dist[st] = nd; prev[st] = cur; heapq.heappush(pq, (nd + h(nx, ny), nd, st))
        o = 1 - L
        if passable(o, ix, iy, k) and via_ok(ix, iy, k):
            st = (o, ix, iy); nd = d + VIA_COST
            if nd < dist.get(st, 1e18):
                dist[st] = nd; prev[st] = cur; heapq.heappush(pq, (nd + h(ix, iy), nd, st))
    return None

tracks = []   # (net, layer, (x1,y1),(x2,y2), width)
vias = []
POWER = {"+3V3", "+5V", "/AMP_VIN"}
order = ["+5V", "/AMP_VIN", "+3V3", "/I2S_BCLK", "/I2S_WS", "/I2S_SPK", "/I2S_MIC", "/SD_SCK", "/SD_MOSI", "/SD_MISO", "/SD_CS", "/MODE", "/DIAL", "/I2C_SDA", "/I2C_SCL", "/LED_DRV", "/LED_A"]
failed = []
for nm in order:
    k = netid[nm]; pads = terms[nm]; w = 0.3 if nm in POWER else 0.25
    tree = set(); first = pads[0]
    fx, fy = cell(first[2], first[3]); tree |= {(0, fx, fy), (1, fx, fy)}
    segs = []
    for (ref, num, x, y) in pads[1:]:
        tx, ty = cell(x, y)
        if (0, tx, ty) in tree or (1, tx, ty) in tree: continue
        path = route(k, tree, (tx, ty))
        if not path: failed.append((nm, ref, num)); continue
        segs.append(path)
        for (L, ix, iy) in path:
            tree.add((L, ix, iy)); paint_cell(L, ix, iy, k)
            paint_cell(1-L, ix, iy, k) if False else None
        # vias occupy both layers
        for a, b in zip(path, path[1:]):
            if a[0] != b[0]:
                paint_cell(0, a[1], a[2], k, [(dx,dy) for dx in range(-6,7) for dy in range(-6,7) if dx*dx+dy*dy<=36]); paint_cell(1, a[1], a[2], k, [(dx,dy) for dx in range(-6,7) for dy in range(-6,7) if dx*dx+dy*dy<=36])
                tree.add((1-a[0], a[1], a[2]))
    # convert paths to tracks/vias
    for path in segs:
        run = [path[0]]
        def flush(run):
            if len(run) < 2: return
            pts = [(r[1]*G, r[2]*G) for r in run]; L = run[0][0]
            comp = [pts[0]]
            for i in range(1, len(pts)-1):
                a, b, c = comp[-1], pts[i], pts[i+1]
                if abs((b[0]-a[0])*(c[1]-b[1]) - (b[1]-a[1])*(c[0]-b[0])) > 1e-6: comp.append(b)
            comp.append(pts[-1])
            for p, q in zip(comp, comp[1:]): tracks.append((nm, L, p, q, w))
        for a, b in zip(path, path[1:]):
            if a[0] != b[0]:
                flush(run); run = [b]; vias.append((nm, a[1]*G, a[2]*G))
            else: run.append(b)
        flush(run)
    # stubs from true pad centres to grid cells
    for (ref, num, x, y) in pads:
        gx, gy = cell(x, y)[0]*G, cell(x, y)[1]*G
        if abs(gx-x) > 1e-4 or abs(gy-y) > 1e-4:
            for L in (0,):
                tracks.append((nm, L, (x, y), (gx, gy), w))
print("routing failures:", failed)

for (nm, L, p, q, w) in tracks:
    t = pcbnew.PCB_TRACK(board); t.SetStart(mm(*p)); t.SetEnd(mm(*q)); t.SetWidth(FromMM(w))
    t.SetLayer(pcbnew.F_Cu if L == 0 else pcbnew.B_Cu); t.SetNet(nets[nm]); board.Add(t)
for (nm, x, y) in vias:
    v = pcbnew.PCB_VIA(board); v.SetPosition(mm(x, y)); v.SetWidth(FromMM(0.8)); v.SetDrill(FromMM(0.4))
    v.SetNet(nets[nm]); board.Add(v)

# ---- GND zones ----------------------------------------------------------
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board); z.SetLayer(layer); z.SetNet(net("GND"))
    z.SetLocalClearance(FromMM(0.25)); z.SetMinThickness(FromMM(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    o = z.Outline(); o.NewOutline()
    for (x, y) in [(0.3,0.3),(BW-0.3,0.3),(BW-0.3,BH-0.3),(0.3,BH-0.3)]: o.Append(FromMM(x), FromMM(y))
    board.Add(z)
pcbnew.ZONE_FILLER(board).Fill(board.Zones())

# ---- silkscreen labels --------------------------------------------------
def silk(text, x, y, size=1.0, angle=0, layer=pcbnew.F_SilkS):
    t = pcbnew.PCB_TEXT(board); t.SetText(text); t.SetPosition(mm(x, y)); t.SetLayer(layer)
    t.SetTextSize(VECTOR2I(FromMM(size), FromMM(size))); t.SetTextThickness(FromMM(size*0.15))
    t.SetTextAngleDegrees(angle); board.Add(t)
def pinlabels(ref, labels, dy, size=0.9):
    for pd, lb in zip(sorted(parts[ref].Pads(), key=lambda p: int(p.GetNumber())), labels):
        p = pd.GetPosition(); silk(lb, ToMM(p.x), ToMM(p.y)+dy, size, 90)
pinlabels("J4", [n for n,_ in MIC_PINS], -4.2); pinlabels("J5", [n for n,_ in AMP_PINS], -4.2)
pinlabels("J3", [n for n,_ in SD_PINS], 4.2)
pinlabels("J1", FEATHER_16, 4.4, 0.8); pinlabels("J2", FEATHER_12, -4.4, 0.8)
silk("USB end", 6.5, 35.5, 1.0, 90)
silk("JP1 1-2: USB 5V", 59.0, 19.6, 0.9, 0); silk("JP1 2-3: 3.3V", 59.0, 21.3, 0.9, 0)
silk("LED+", 66.04, 24.2, 0.9, 0); silk("LED-", 66.04, 33.9, 0.9, 0)
pinlabels("J7", [n for n,_ in LCD_PINS], -4.4); pinlabels("J8", [n for n,_ in MODE_PINS], -4.4)
pinlabels("J9", ["PULSE", "GND"], -4.6)
silk("Super Secret Gossip Phone  rev A", 36, 83.0, 1.2)
for ref in ('H1','H2','H3','H4'):
    fpx = board.FindFootprintByReference(ref)
    fpx.Reference().SetVisible(False)
board.Save(sys.argv[1])
print("saved", sys.argv[1], "tracks", len(tracks), "vias", len(vias))
