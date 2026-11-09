import re, uuid, sys
sys.path.insert(0, __file__.rsplit('/',1)[0])
from design import *
SYM = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols/"
PROJ = "supersecretgossipphone"
ROOT = str(uuid.uuid4())
def U(): return str(uuid.uuid4())

_cache = {}
def lib_symbol(lib, name):
    key = (lib, name)
    if key in _cache: return _cache[key]
    s = open(SYM + lib + ".kicad_sym").read()
    i = s.index('(symbol "%s"' % name)
    d = 0; j = i
    while True:
        c = s[j]
        if c == '(': d += 1
        elif c == ')':
            d -= 1
            if d == 0: break
        j += 1
    blk = s[i:j+1].replace('(symbol "%s"' % name, '(symbol "%s:%s"' % (lib, name), 1)
    pins = []
    for m in re.finditer(r'\(pin\s+\w+\s+\w+\s*\(at\s+([-\d.]+)\s+([-\d.]+)\s+(\d+)\).*?\(number\s+"([^"]+)"', blk, re.S):
        pins.append((m.group(4), float(m.group(1)), float(m.group(2)), int(m.group(3))))
    _cache[key] = (blk, pins)
    return _cache[key]

out_syms = {}; body = []
def fmt(v): return ("%.4f" % v).rstrip('0').rstrip('.')

def wire(x1,y1,x2,y2):
    body.append('(wire (pts (xy %s %s) (xy %s %s)) (stroke (width 0) (type default)) (uuid "%s"))' % (fmt(x1),fmt(y1),fmt(x2),fmt(y2),U()))
def label(net,x,y,ox):
    if ox >= 0: ang, just = 0, "left bottom"
    else: ang, just = 180, "right bottom"
    body.append('(label "%s" (at %s %s %d) (effects (font (size 1.27 1.27)) (justify %s)) (uuid "%s"))' % (net,fmt(x),fmt(y),ang,just,U()))
def nc(x,y): body.append('(no_connect (at %s %s) (uuid "%s"))' % (fmt(x),fmt(y),U()))
def text(t,x,y,size=1.27,just="left"):
    body.append('(text "%s" (at %s %s 0) (effects (font (size %s %s)) (justify %s)) (uuid "%s"))' % (t,fmt(x),fmt(y),size,size,just,U()))
def junction(x,y): body.append('(junction (at %s %s) (diameter 0) (color 0 0 0 0) (uuid "%s"))' % (fmt(x),fmt(y),U()))

def place(lib, name, ref, value, fp, x, y, nets=None, names=None, stub=2.54, hide_ref=False):
    """nets: dict pin-number -> net or None (no-connect). names: dict pin-number -> function text."""
    x = round(x/1.27)*1.27; y = round(y/1.27)*1.27
    blk, pins = lib_symbol(lib, name)
    out_syms[(lib,name)] = blk
    props = ""
    ys = [y - py for (_,px,py,_) in pins] or [y]
    top, bot = min(ys), max(ys)
    def prop(k, v, ax, ay, hide=False):
        return '(property "%s" "%s" (at %s %s 0) (effects (font (size 1.27 1.27))%s))' % (k, v, fmt(ax), fmt(ay), " (hide yes)" if hide else "")
    pin_s = "".join('(pin "%s" (uuid "%s"))' % (p[0], U()) for p in pins)
    body.append('(symbol (lib_id "%s:%s") (at %s %s 0) (unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid "%s") %s %s %s %s %s (instances (project "%s" (path "/%s" (reference "%s") (unit 1)))))' % (
        lib, name, fmt(x), fmt(y), U(),
        prop("Reference", ref, x, top - 3.5, hide_ref), prop("Value", value, x, bot + 3.5),
        prop("Footprint", fp, x, y, True), prop("Datasheet", "~", x, y, True), pin_s, PROJ, ROOT, ref, ))
    for (num, px, py, ang) in pins:
        cx, cy = x + px, y - py
        dx = {0:1,90:0,180:-1,270:0}[ang]; dy = {0:0,90:1,180:0,270:-1}[ang]
        ox, oy = -dx, dy          # outward direction in schematic coordinates
        if names and num in names:
            text(names[num], cx - ox*0.0 - (4.5 if ox>0 else -4.5) if False else (cx - 5.9 if ox > 0 else cx + 5.9), cy - 0.2, 1.0, "right" if ox > 0 else "left")
        net = (nets or {}).get(num, "__none__")
        if net == "__none__" or net is None:
            nc(cx, cy); continue
        ex, ey = cx + ox*stub, cy + oy*stub
        wire(cx, cy, ex, ey); label(net, ex, ey, ox if ox else 1)
    return

# ---- parts --------------------------------------------------------------
def conn(lib_name, ref, desc, fp, x, y, pinlist, kind="Socket"):
    n = len(pinlist)
    name = "Conn_01x%02d_%s" % (n, kind)
    nets = {str(i+1): net for i,(nm,net) in enumerate(pinlist)}
    names = {str(i+1): nm for i,(nm,net) in enumerate(pinlist)}
    place("Connector", name, ref, desc, fp, x, y, nets, names)

PS = "Connector_PinSocket_2.54mm:PinSocket_1x%02d_P2.54mm_Vertical"
f16 = [(n, FEATHER_NETS.get(n)) for n in FEATHER_16]
f12 = [(n, FEATHER_NETS.get(n)) for n in FEATHER_12]
conn(None,"J1","Feather 16-pin socket", PS % 16, 60, 75, f16)
conn(None,"J2","Feather 12-pin socket", PS % 12, 60, 150, f12)
conn(None,"J3","microSD breakout (Adafruit 254 pinout)", PS % 8, 170, 60, SD_PINS)
conn(None,"J4","I2S mic breakout (SPH0645)", PS % 6, 170, 110, MIC_PINS)
conn(None,"J5","I2S amp breakout (MAX98357A)", PS % 7, 170, 155, AMP_PINS)
place("Connector","Conn_01x03_Pin","JP1","Amp power select","Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
      270, 60, {"1":"+5V","2":"AMP_VIN","3":"+3V3"}, {"1":"5V (USB)","2":"to amp VIN","3":"3V3"})
text("Shunt 1-2: amp on USB 5 V (louder).  Shunt 2-3: amp on 3.3 V.", 235, 78, 1.0)
place("Device","C_Polarized_Small","C1","10uF","Capacitor_THT:CP_Radial_D5.0mm_P2.00mm", 270, 115, {"1":"AMP_VIN","2":"GND"})
conn(None,"J7","20x4 I2C LCD (backpack)", PS % 4, 170, 200, LCD_PINS)
place("Connector","Conn_01x03_Pin","J8","Mode toggle (on/on)","Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
      170, 240, nets_of_sw := {"1":"GND","2":"MODE","3":"+3V3"}, {"1":"LISTEN","2":"COM","3":"RECORD"})
place("Device","RotaryEncoder_Switch","ENC1","EC11 dial","Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm",
      240, 235, {"A":"ENC_A","B":"ENC_B","C":"GND","S1":"ENC_SW","S2":"GND"})
place("Device","R_Small","R1","330R","Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal", 270, 190, {"1":"LED_DRV","2":"LED_A"})
place("Connector","Conn_01x02_Pin","J6","External recording LED","Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical", 320, 105, {"1":"LED_A","2":"GND"}, {"1":"LED +","2":"LED -"})
for i,(ref,net,lab) in enumerate([("SW1","BTN_REC","Record / Stop"),("SW2","BTN_PLAY","Play / Stop"),("SW3","BTN_NEXT","Next file")]):
    place("Switch","SW_Push",ref,lab,"Button_Switch_THT:SW_PUSH_6mm", 285, 150+i*20, {"1":net,"2":"GND"})
for i in range(4):
    place("Mechanical","MountingHole","H%d"%(i+1),"M2.5","MountingHole:MountingHole_2.7mm_M2.5", 345, 60+i*15, {})

# power anchors: symbol + PWR_FLAG + label
def anchor(net, lib, sym, x, y):
    x = round(x/1.27)*1.27; y = round(y/1.27)*1.27
    blk,_ = lib_symbol("power", sym); out_syms[("power",sym)] = blk
    blk,_ = lib_symbol("power", "PWR_FLAG"); out_syms[("power","PWR_FLAG")] = blk
    def inst(name, ref, px, py):
        body.append('(symbol (lib_id "power:%s") (at %s %s 0) (unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid "%s") (property "Reference" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)) (hide yes))) (property "Value" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)))) (property "Footprint" "" (at %s %s 0) (effects (font (size 1.27 1.27)) (hide yes))) (property "Datasheet" "" (at %s %s 0) (effects (font (size 1.27 1.27)) (hide yes))) (pin "1" (uuid "%s")) (instances (project "%s" (path "/%s" (reference "%s") (unit 1)))))' % (
            name, fmt(px), fmt(py), U(), ref, fmt(px), fmt(py), name, fmt(px), fmt(py+ (4 if sym=="GND" else -4)), fmt(px), fmt(py), fmt(px), fmt(py), U(), PROJ, ROOT, ref))
    global pw; pw += 2
    inst(sym, "#PWR%02d" % (pw-1), x, y)
    inst("PWR_FLAG", "#FLG%02d" % pw, x+5.08, y)
    wire(x, y, x+5.08, y); wire(x+5.08, y, x+10.16, y); junction(x+5.08, y)
    label(net, x+10.16, y, 1)
pw = 0
anchor("GND","power","GND", 330, 200)
anchor("+3V3","power","+3V3", 330, 215)
anchor("+5V","power","+5V", 330, 230)

text("Super Secret Gossip Phone - Feather ESP32-S3 carrier board", 20, 25, 3.0)
text("Feather plugs into J1/J2. Breakout boards (microSD, I2S mic, I2S amp) plug into sockets J3-J5.", 20, 33, 1.5)
text("Pins match mp3_recorder.ino: SD CS=D10, I2S BCLK=D9, WS=D6, mic data=D12, amp data=D5, buttons A0/A1/A2.", 20, 38, 1.5)

hdr = '(kicad_sch (version 20250114) (generator "eeschema") (generator_version "9.0") (uuid "%s") (paper "A3") (title_block (title "Super Secret Gossip Phone carrier") (rev "A") (company "gregariouspanda")) (lib_symbols %s)' % (ROOT, "\n".join(out_syms.values()))
sch = hdr + "\n" + "\n".join(body) + '\n(sheet_instances (path "/" (page "1")))\n(embedded_fonts no))\n'
open(sys.argv[1], "w").write(sch)
print("wrote", sys.argv[1], len(body), "items")
