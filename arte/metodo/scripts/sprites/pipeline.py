# Fluxo v3 por direção: folha de manequins (sprites do jogo) -> master manequim HD -> Thief -> recorte fino -> quadros + GIFs.
# Uso: python v3/pipeline.py S SW W ...   (pula etapas já feitas; apague o arquivo da etapa pra refazer)
import subprocess, json, os, sys, urllib.request, time
import numpy as np, cv2
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recorte_fino import recortar
RAIZ = "C:/dr-sprites"; V3 = RAIZ + "/v3"
FLOW = r"C:/dr-sprites/flow/flow.mjs"
PROJ = "ce7ecfba-9ec3-4a9d-89e2-8e4a4a8fffd7"
TURN = "6675da81-3156-42b3-840f-45c05b55482d"   # ficha do Thief com aljava e cota de malha (frente, 3/4, perfil, costas)
VISTA = {"S": "facing the viewer (front view)", "SW": "turned three-quarter toward the viewer's left (front-left view)",
         "W": "facing the viewer's left (left side profile)", "NW": "turned away, three-quarter back view toward the upper left",
         "N": "facing away from the viewer (back view, we see his back and the quiver)", "NE": "turned away, three-quarter back view toward the upper right",
         "E": "facing the viewer's right (right side profile)", "SE": "turned three-quarter toward the viewer's right (front-right view)"}
ACAO = {"andar": ([4,5,6,7,8,9], "walking (walk cycle)"), "parado": ([1,2,3,1,2,3], "standing idle and breathing"), "correr": ([44,45,46,47,48,49], "running fast (run cycle, body leaning forward, long strides)")}
def call(tool, args):
    for t in range(3):
        r = subprocess.run(["node", FLOW, tool, json.dumps(args)], capture_output=True, text=True, encoding="utf-8")
        s = r.stdout; ks = [k for k in (s.find("["), s.find("{")) if k >= 0]
        if ks:
            try: return json.loads(s[min(ks):])
            except Exception: pass
        print("  retry", tool, (r.stderr or s)[-300:], flush=True); time.sleep(20)
    raise RuntimeError("falhou " + tool)
def gerar(prompt, refs, out):
    res = call("flow_generate_image", {"project_id": PROJ, "prompt": prompt, "aspect": "16:9", "count": 1, "reference_media_ids": refs})
    urllib.request.urlretrieve(res[0]["url"], out); return res[0]["mediaId"]
def subir(arq):
    nome = os.path.basename(arq)
    for t in range(3):
        d = call("flow_get_project", {"project_id": PROJ})
        for g in d["generations"]:
            if g["caption"] == nome: return g["mediaId"]
        if t: subprocess.run(["node", RAIZ + "/_limpar.mjs"], capture_output=True, cwd=RAIZ)
        subprocess.run(["node", RAIZ + "/subir.mjs", arq], capture_output=True, cwd=RAIZ); time.sleep(5)
    raise RuntimeError("upload sumiu " + nome)

sys.path.insert(0, r"C:/dr-sprites/jogo")
from spr_decode import load_spr, decode_frame
_SPR = load_spr(r"C:/DragonRaja/Char/001000.spr")
DIRS = ["S","SW","W","NW","N","NE","E","SE"]
PX = 5.6   # px da folha final por px do sprite (calibrado na direção S aprovada)
def altura_alvo(d, quadros):
    hs = []
    for q in quadros:
        a = decode_frame(_SPR[0], int(_SPR[1][DIRS.index(d)*75+q]))[0][..., 3]
        ys = np.nonzero(a.max(1) > 0)[0]; hs.append(ys.max() - ys.min())
    return float(np.median(hs)) * PX
def altura_folha(arq, n):
    al = recortar(cv2.imread(arq))[..., 3]; W = al.shape[1]; hs = []
    for i in range(n):
        c = al[:, int(W*i/n):int(W*(i+1)/n)]; ys = np.nonzero(c.max(1) > 128)[0]
        if len(ys): hs.append(ys.max() - ys.min())
    return float(np.median(hs))
ESCALA = {("SW", "correr"): 6.0}
ids = json.load(open(V3 + "/ids.json")) if os.path.exists(V3 + "/ids.json") else {}
def salva(): json.dump(ids, open(V3 + "/ids.json", "w"), indent=1)
for d in sys.argv[1:]:
    pasta = f"{V3}/{d}"; os.makedirs(pasta, exist_ok=True)
    for acao, (quadros, desc) in ACAO.items():
        n = len(quadros); base = f"{pasta}/{acao}"; baixa = f"{pasta}/{d}_{acao}_manequim_baixa.png"
        if not os.path.exists(baixa):
            subprocess.run([sys.executable, RAIZ + "/v2/folha_manequins.py", d, ",".join(map(str, quadros)), baixa, str(ESCALA.get((d, acao), 7.1))], check=True)
        k = f"{d}_{acao}"
        if k + "_baixa" not in ids: ids[k + "_baixa"] = subir(baixa); salva()
        if not os.path.exists(base + "_master.png"):
            ids[k + "_master"] = gerar(
                f"The reference image is a low-resolution, pixelated sprite sheet of {n} animation frames of a man {desc}, seen from a high "
                f"three-quarter top-down camera (isometric RPG view), {VISTA[d]}. Recreate this exact sheet in high resolution: {n} smooth, detailed, "
                "clean WHITE 3D mannequin figures (matte white artist mannequin with defined anatomy, clear knees, elbows, hands and feet), on the same "
                f"plain flat medium gray background. Keep EXACTLY the same {n} poses in the same left-to-right order, same positions, same spacing, "
                "same SMALL size, same ground line and the same camera angle. Keep EXACTLY the body rotation and facing direction of each figure as in the reference; do NOT turn them toward the camera. Each leg and arm exactly as in its frame: which "
                "foot is forward, knee bend, foot lifted, arm swing. Do not add, remove, merge or reorder figures. No shadows, no floor, no text.",
                [ids[k + "_baixa"]], base + "_master.png"); salva(); print(d, acao, "master", flush=True)
        alvo = altura_alvo(d, quadros) * ESCALA.get((d, acao), 7.1) / 7.1
        for tentativa in range(4):
            if not os.path.exists(base + "_thief.png"):
                ids[k + "_thief"] = gerar(
                    f"Image 1 is a sprite sheet of {n} white mannequins {desc}, {VISTA[d]}. Image 2 is the character design sheet (front, three-quarter, "
                    "side and back views): a thief in red and gold armored tunic, tall dark blue hat with gold plate and red tassel, a red and gold "
                    "QUIVER with arrows on his back with its strap across the chest, CHAINMAIL trousers, brown leather gloves, brown boots with gold "
                    f"greaves. Dress EVERY one of the {n} mannequins of image 1 as this exact character, {VISTA[d]}. CRITICAL: keep each figure exactly "
                    "the same SMALL size, position, spacing and order as the mannequins in image 1 (do not zoom in, keep the empty gray space), exactly "
                    "the same pose, leg and arm positions, facing direction and high three-quarter top-down camera angle. All figures identical in "
                    "costume, face, proportions and colors; only the pose changes. Same plain flat uniform medium gray background, no floor, no "
                    "shadows, no text. Realistic Unreal Engine 5 render quality.",
                    [ids[k + "_master"], TURN], base + "_thief.png"); salva(); print(d, acao, "thief", flush=True)
            h = altura_folha(base + "_thief.png", n)
            if abs(h / alvo - 1) <= 0.10 or tentativa == 3: break
            print(d, acao, "tamanho errado", int(h), "esperado", int(alvo), "-> refazendo", flush=True)
            os.replace(base + "_thief.png", base + f"_thief_ruim{tentativa}.png")
            if os.path.exists(base + "_recortada.png"): os.remove(base + "_recortada.png")

        if not os.path.exists(base + "_recortada.png"):
            cv2.imwrite(base + "_recortada.png", recortar(cv2.imread(base + "_thief.png")))
    print("DIRECAO PRONTA", d, flush=True)
print("FIM", flush=True)
