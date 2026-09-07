import argparse
import json
import os
import shutil
import sys
import time
import zipfile
from datetime import datetime
from pathlib import Path

import requests

# ------------------------------------------------------------------
# Config
# ------------------------------------------------------------------

client_things = Path(r"B:\Github\blacktalon-workspace\client\data\things\1098")

src_files = [
    Path(r"B:\Github\blacktalon-workspace\server\data\items\items.otb"),
    Path(r"B:\Github\blacktalon-workspace\server\data\items\items.xml"),
    client_things / "Tibia.dat",
    client_things / "Tibia.otfi",
    # Catalogo do spr fragmentado. O .spr monolitico nao existe mais: o client
    # (e o RME) leem o .cat, que aponta para os arquivos em sprites/.
    client_things / "Tibia.cat",
]

# Pastas copiadas inteiras (origem -> nome da subpasta no destino). Os nomes
# dos fragmentos estao gravados dentro do .cat como "sprites/Tibia_N.spr" e
# sao resolvidos relativo ao catalogo, entao a subpasta precisa se chamar
# exatamente "sprites" e ficar ao lado do Tibia.cat.
src_dirs = [
    (client_things / "sprites", "sprites"),
]

extra_files = [
    (Path(r"B:\Github\blacktalon-workspace\server\data\scripts\creatures_table.json"),
     Path(r"B:\Github\rme_redux\data")),
]

dest = Path(r"B:\Github\rme_redux\data\1098")
zip_path = dest.parent / "BT_SPRDAT_UPDATED.zip"

# URL do webhook: variavel de ambiente DISCORD_WEBHOOK_URL ou o arquivo abaixo
# (uma linha com a URL). O arquivo esta no .gitignore.
webhook_file = Path(__file__).with_name("discord_webhook.txt")

# Historico dos IDs das mensagens postadas por este script, para poder
# apaga-las depois sem precisar de bot.
sent_file = Path(__file__).with_name("discord_sent.json")

# Token de bot (opcional, arquivo de uma linha). So e necessario para apagar
# mensagens que o script postou ANTES de existir o historico acima -- um
# webhook nao consegue ler o canal, entao nao tem como descobrir os IDs
# sozinho. O bot precisa de: Ver Canal, Ler Historico e Gerenciar Mensagens.
bot_token_file = Path(__file__).with_name("discord_bot_token.txt")

# Como entregar o pacote:
#   "link"   - sobe o zip inteiro para um host externo e posta so o link no
#              canal (1 arquivo unico para quem baixar). Padrao.
#   "attach" - anexa no proprio Discord. Como webhook aceita no maximo 100 MB
#              (teto da API, mesmo com boost tier 3), um zip maior sai
#              dividido em partes .001/.002/...
DELIVERY = "link"

# Host usado quando DELIVERY == "link":
#   "gofile"    - sem conta, sem limite pratico; o link some depois de ~10
#                 dias sem ninguem acessar.
#   "litterbox" - sem conta, ate 1 GB, expira no prazo de LITTERBOX_TIME.
UPLOAD_HOST = "gofile"
LITTERBOX_TIME = "72h"   # 1h, 12h, 24h ou 72h

# Zona preferida do Gofile. A API devolve a lista embaralhada, com servidores
# na Europa ("eu") e na America do Norte ("na"); do Brasil o "na" costuma
# render bem mais no upload. Deixe "" para aceitar qualquer um.
GOFILE_ZONE = "na"

# Usado so quando DELIVERY == "attach":
#   sem boost / tier 1 = 10 MB, tier 2 = 50 MB, tier 3 = 100 MB.
# Se o Discord recusar (413), cai para FALLBACK_UPLOAD_MB e divide em partes.
MAX_UPLOAD_MB = 100
FALLBACK_UPLOAD_MB = 10

# Margem para o overhead do multipart e do texto da mensagem.
UPLOAD_MARGIN_BYTES = 256 * 1024

# Quem deve ser notificado na mensagem principal.
# Usuarios: o ID numerico (Discord > Config > Avancado > Modo desenvolvedor,
# depois botao direito no usuario > Copiar ID do usuario).
# Cargos: coloque o ID do cargo em MENTION_ROLE_IDS.
# Webhook nao envia DM; a mencao notifica a pessoa dentro do canal.
MENTION_USER_IDS = [
    "169134750135615488",   # IAGO
    "266766307905503255",   # JACQEN
]
MENTION_ROLE_IDS = []   # ex.: ["987654321098765432"]
MENTION_EVERYONE = False


# ------------------------------------------------------------------
# Discord
# ------------------------------------------------------------------

def load_webhook_url():
    url = os.environ.get("DISCORD_WEBHOOK_URL", "").strip()
    if url:
        return url
    if webhook_file.exists():
        return webhook_file.read_text(encoding="utf-8").strip()
    return ""


def mention_prefix():
    """Monta as mencoes que abrem a mensagem principal."""
    parts = []
    if MENTION_EVERYONE:
        parts.append("@everyone")
    parts += [f"<@&{rid}>" for rid in MENTION_ROLE_IDS]
    parts += [f"<@{uid}>" for uid in MENTION_USER_IDS]
    return " ".join(parts) + "\n" if parts else ""


def allowed_mentions():
    """Libera so quem esta configurado (evita ping acidental)."""
    allowed = {"parse": ["everyone"] if MENTION_EVERYONE else []}
    if MENTION_USER_IDS:
        allowed["users"] = [str(u) for u in MENTION_USER_IDS]
    if MENTION_ROLE_IDS:
        allowed["roles"] = [str(r) for r in MENTION_ROLE_IDS]
    return allowed


def load_bot_token():
    token = os.environ.get("DISCORD_BOT_TOKEN", "").strip()
    if token:
        return token
    if bot_token_file.exists():
        return bot_token_file.read_text(encoding="utf-8").strip()
    return ""


def load_sent():
    if sent_file.exists():
        try:
            return json.loads(sent_file.read_text(encoding="utf-8"))
        except Exception:
            return []
    return []


def save_sent(entries):
    sent_file.write_text(json.dumps(entries, indent=2), encoding="utf-8")


def record_sent(message_id, label):
    entries = load_sent()
    entries.append({
        "id": str(message_id),
        "label": label,
        "when": datetime.now().strftime("%d/%m/%Y %H:%M"),
    })
    save_sent(entries)


def is_ok(status):
    return 200 <= status < 300


def discord_post(url, content=None, file_path=None, retries=3):
    """Posta no webhook e devolve o status HTTP (0 se esgotou o rate limit).

    Trata 429 respeitando retry_after; o 413 e devolvido ao chamador para
    que ele possa reenviar dividindo o arquivo.
    """
    for attempt in range(retries):
        payload = {"allowed_mentions": allowed_mentions()}
        if content:
            payload["content"] = content
        # wait=true faz o Discord devolver a mensagem criada, com o id que
        # precisamos guardar para conseguir apagar depois.
        post_url = url + ("&" if "?" in url else "?") + "wait=true"
        handle = None
        try:
            if file_path is not None:
                handle = open(file_path, "rb")
                resp = requests.post(
                    post_url,
                    data={"payload_json": json.dumps(payload)},
                    files={"file": (file_path.name, handle)},
                    timeout=600)
            else:
                resp = requests.post(post_url, json=payload, timeout=600)
        finally:
            if handle is not None:
                handle.close()

        if resp.status_code == 429:
            wait = 5.0
            try:
                wait = float(resp.json().get("retry_after", 5))
            except Exception:
                pass
            print(f"  rate limit, aguardando {wait:.1f}s...")
            time.sleep(wait + 0.5)
            continue

        if resp.status_code >= 400:
            print(f"  ERRO {resp.status_code}: {resp.text[:400]}")
        elif is_ok(resp.status_code):
            try:
                mid = resp.json().get("id")
                if mid:
                    label = file_path.name if file_path else "mensagem"
                    record_sent(mid, label)
            except Exception:
                pass
        return resp.status_code

    print("  Falhou apos varias tentativas de rate limit.")
    return 0


# ------------------------------------------------------------------
# Limpeza do canal
# ------------------------------------------------------------------

def delete_tracked(url):
    """Apaga as mensagens registradas em discord_sent.json.

    Um webhook pode apagar as proprias mensagens, desde que saiba o id.
    """
    entries = load_sent()
    if not entries:
        print("Nenhuma mensagem registrada em discord_sent.json.")
        return 0

    print(f"Apagando {len(entries)} mensagens registradas...")
    restantes = []
    apagadas = 0
    for entry in entries:
        endpoint = f"{url}/messages/{entry['id']}"
        resp = requests.delete(endpoint, timeout=60)
        if resp.status_code == 429:
            wait = 5.0
            try:
                wait = float(resp.json().get("retry_after", 5))
            except Exception:
                pass
            time.sleep(wait + 0.5)
            resp = requests.delete(endpoint, timeout=60)
        # 404 = ja nao existe; tratamos como sucesso para sair da lista
        if is_ok(resp.status_code) or resp.status_code == 404:
            apagadas += 1
            sys.stdout.write(f"\r  {apagadas}/{len(entries)}")
            sys.stdout.flush()
        else:
            print(f"\n  falhou {entry['id']} ({resp.status_code}): "
                  f"{resp.text[:200]}")
            restantes.append(entry)
        time.sleep(0.35)   # folga para o rate limit

    print()
    save_sent(restantes)
    return apagadas


def webhook_identity(url):
    """Devolve (webhook_id, channel_id) consultando o proprio webhook."""
    resp = requests.get(url, timeout=60)
    if not is_ok(resp.status_code):
        print(f"  Nao consegui ler o webhook ({resp.status_code}).")
        return "", ""
    data = resp.json()
    return str(data.get("id", "")), str(data.get("channel_id", ""))


def delete_via_bot(url, token, limit_scan=1000, dry_run=False):
    """Varre o canal com um bot e apaga tudo que veio deste webhook.

    Necessario para mensagens postadas antes de o historico existir.
    """
    webhook_id, channel_id = webhook_identity(url)
    if not channel_id:
        return 0

    headers = {"Authorization": f"Bot {token}"}
    base = f"https://discord.com/api/v10/channels/{channel_id}"

    alvos = []
    before = None
    varridas = 0
    while varridas < limit_scan:
        params = {"limit": 100}
        if before:
            params["before"] = before
        resp = requests.get(f"{base}/messages", headers=headers,
                            params=params, timeout=60)
        if resp.status_code == 401:
            print("  Token de bot invalido.")
            return 0
        if resp.status_code == 403:
            print("  O bot nao tem acesso ao canal (precisa de Ver Canal, "
                  "Ler Historico e Gerenciar Mensagens).")
            return 0
        if not is_ok(resp.status_code):
            print(f"  ERRO ao ler o canal ({resp.status_code}): "
                  f"{resp.text[:200]}")
            return 0

        lote = resp.json()
        if not lote:
            break
        varridas += len(lote)
        before = lote[-1]["id"]
        alvos += [m["id"] for m in lote
                  if str(m.get("webhook_id", "")) == webhook_id]
        sys.stdout.write(f"\r  varridas {varridas} mensagens, "
                         f"{len(alvos)} do webhook")
        sys.stdout.flush()
        if len(lote) < 100:
            break

    print()
    if not alvos:
        print("  Nada deste webhook encontrado no canal.")
        return 0

    if dry_run:
        print(f"  [dry-run] {len(alvos)} mensagens seriam apagadas.")
        return len(alvos)

    apagadas = 0
    # bulk-delete: ate 100 por chamada, so mensagens com menos de 14 dias
    for i in range(0, len(alvos), 100):
        lote = alvos[i:i + 100]
        if len(lote) >= 2:
            resp = requests.post(f"{base}/messages/bulk-delete",
                                 headers=headers,
                                 json={"messages": lote}, timeout=60)
            if is_ok(resp.status_code):
                apagadas += len(lote)
                print(f"  apagadas {apagadas}/{len(alvos)}")
                time.sleep(1.0)
                continue
            print(f"  bulk-delete falhou ({resp.status_code}), "
                  f"apagando uma a uma...")
        for mid in lote:
            resp = requests.delete(f"{base}/messages/{mid}",
                                   headers=headers, timeout=60)
            if resp.status_code == 429:
                wait = 5.0
                try:
                    wait = float(resp.json().get("retry_after", 5))
                except Exception:
                    pass
                time.sleep(wait + 0.5)
                resp = requests.delete(f"{base}/messages/{mid}",
                                       headers=headers, timeout=60)
            if is_ok(resp.status_code) or resp.status_code == 404:
                apagadas += 1
                sys.stdout.write(f"\r  apagadas {apagadas}/{len(alvos)}")
                sys.stdout.flush()
            time.sleep(0.35)
        print()

    return apagadas


def limpar_canal(url, dry_run=False):
    """Apaga o que o script postou: historico local + varredura com bot."""
    total = 0 if dry_run else delete_tracked(url)

    token = load_bot_token()
    if token:
        print("\nVarrendo o canal com o bot (pega tambem o que foi postado "
              "antes do historico)...")
        total += delete_via_bot(url, token, dry_run=dry_run)
    else:
        print("\nSem token de bot configurado: so da para apagar o que esta "
              "no historico local.")
        print(f"Para limpar o que ja estava no canal antes, crie o bot, "
              f"convide-o com Gerenciar Mensagens e salve o token em "
              f"{bot_token_file.name}.")

    print(f"\nTotal {'que seria apagado' if dry_run else 'apagado'}: {total}")
    return total


# ------------------------------------------------------------------
# Upload externo (arquivo unico)
# ------------------------------------------------------------------

BOUNDARY = "----RmeReduxBoundary7MA4YWxkTrZu0gW"


class MultipartStream:
    """Corpo multipart file-like: cabecalho + arquivo + rodape.

    Precisa ser file-like (e nao um generator) para que o requests use
    Content-Length em vez de Transfer-Encoding: chunked -- varios hosts
    recusam chunked com 400.
    """

    def __init__(self, file_path, file_field="file", fields=None):
        fields = fields or {}
        self.path = file_path
        self.size = file_path.stat().st_size

        head = b""
        for name, value in fields.items():
            head += (f"--{BOUNDARY}\r\n"
                     f'Content-Disposition: form-data; name="{name}"\r\n\r\n'
                     f"{value}\r\n").encode("utf-8")
        head += (f"--{BOUNDARY}\r\n"
                 f'Content-Disposition: form-data; name="{file_field}"; '
                 f'filename="{file_path.name}"\r\n'
                 f"Content-Type: application/octet-stream\r\n\r\n"
                 ).encode("utf-8")
        self.head = head
        self.tail = f"\r\n--{BOUNDARY}--\r\n".encode("utf-8")
        self.total = len(head) + self.size + len(self.tail)

        self.fh = open(file_path, "rb")
        self.stage = 0          # 0 = head, 1 = arquivo, 2 = tail, 3 = fim
        self.sent = 0
        self.start = time.time()
        self.mark_time = self.start   # ultima amostra, para a taxa atual
        self.mark_sent = 0
        self.rate = 0.0               # MB/s suavizado

    def __len__(self):
        return self.total

    def _progress(self):
        agora = time.time()
        janela = agora - self.mark_time
        if janela >= 0.5:
            atual = (self.sent - self.mark_sent) / janela / 1024 / 1024
            # media exponencial: estabiliza sem mascarar quedas reais
            self.rate = atual if self.rate == 0 else self.rate * 0.7 + atual * 0.3
            self.mark_time, self.mark_sent = agora, self.sent

        pct = min(self.sent * 100 // max(self.size, 1), 100)
        restante = self.size - self.sent
        eta = restante / (self.rate * 1024 * 1024) if self.rate > 0.01 else 0
        bar = "#" * (pct // 2) + "-" * (50 - pct // 2)
        sys.stdout.write(f"\r[{bar}] {pct}% - {self.rate:.1f} MB/s"
                         f" - faltam {int(eta // 60)}m{int(eta % 60):02d}s ")
        sys.stdout.flush()

    def read(self, amount=-1):
        if self.stage == 0:
            self.stage = 1
            return self.head
        if self.stage == 1:
            chunk = self.fh.read(1024 * 1024 if amount in (-1, None)
                                 else max(amount, 65536))
            if chunk:
                self.sent += len(chunk)
                self._progress()
                return chunk
            self.stage = 2
        if self.stage == 2:
            self.stage = 3
            return self.tail
        return b""

    def close(self):
        if not self.fh.closed:
            self.fh.close()


def stream_upload(url, file_path, file_field="file", fields=None):
    """POST multipart lendo o arquivo em pedacos, com barra de progresso."""
    body = MultipartStream(file_path, file_field, fields)
    headers = {
        "Content-Type": f"multipart/form-data; boundary={BOUNDARY}",
        "Content-Length": str(len(body)),
    }
    try:
        resp = requests.post(url, data=body, headers=headers, timeout=3600)
    finally:
        body.close()
    sys.stdout.write("\n")
    return resp


def upload_gofile(file_path):
    """Sobe para o Gofile e devolve a URL da pagina de download."""
    servers = requests.get("https://api.gofile.io/servers",
                           timeout=60).json()["data"]["servers"]
    na_zona = [s for s in servers if s.get("zone") == GOFILE_ZONE]
    server = (na_zona or servers)[0]["name"]
    print(f"Enviando para o Gofile ({server})...")
    resp = stream_upload(f"https://{server}.gofile.io/contents/uploadfile",
                         file_path)
    data = resp.json()
    if data.get("status") != "ok":
        print(f"  ERRO do Gofile: {str(data)[:400]}")
        return ""
    return data["data"].get("downloadPage", "")


def upload_litterbox(file_path):
    """Sobe para o Litterbox (catbox temporario) e devolve a URL direta."""
    print(f"Enviando para o Litterbox (expira em {LITTERBOX_TIME})...")
    resp = stream_upload(
        "https://litterbox.catbox.moe/resources/internals/api.php",
        file_path,
        file_field="fileToUpload",
        fields={"reqtype": "fileupload", "time": LITTERBOX_TIME})
    url = resp.text.strip()
    if not url.startswith("http"):
        print(f"  ERRO do Litterbox: {url[:400]}")
        return ""
    return url


def upload_external(file_path):
    if UPLOAD_HOST == "litterbox":
        return upload_litterbox(file_path)
    if UPLOAD_HOST == "gofile":
        return upload_gofile(file_path)
    print(f"  UPLOAD_HOST desconhecido: {UPLOAD_HOST}")
    return ""


def split_file(path, chunk_bytes):
    """Divide o arquivo em .001, .002, ... e devolve a lista de partes."""
    parts = []
    index = 1
    with open(path, "rb") as fh:
        while True:
            chunk = fh.read(chunk_bytes)
            if not chunk:
                break
            part = path.with_suffix(path.suffix + f".{index:03d}")
            part.write_bytes(chunk)
            parts.append(part)
            index += 1
    return parts


def send_to_discord(url, zip_file, notify_only=False, keep_parts=False):
    size = zip_file.stat().st_size
    stamp = datetime.now().strftime("%d/%m/%Y %H:%M")
    header = (mention_prefix() +
              f"**BT_SPRDAT_UPDATED** - {stamp}\n"
              f"items.otb / items.xml / Tibia.dat / Tibia.otfi / "
              f"Tibia.cat + sprites/ / creatures_table.json\n"
              f"Tamanho: {size / 1024 / 1024:.1f} MB")

    # Quem extrair precisa manter a hierarquia: o .cat referencia os
    # fragmentos por caminho relativo ("sprites/Tibia_N.spr").
    header += ("\nExtraia mantendo a pasta `sprites/` ao lado do `Tibia.cat`.")

    if notify_only:
        print("\nNotificando o Discord (sem anexo)...")
        return is_ok(discord_post(url, content=header + f"\nLocal: `{zip_file}`"))

    if DELIVERY == "link":
        link = upload_external(zip_file)
        if not link:
            print("Upload externo falhou; nada foi postado no canal.")
            return False
        print(f"Link: {link}")
        extra = ""
        if UPLOAD_HOST == "litterbox":
            extra = f" (expira em {LITTERBOX_TIME})"
        return is_ok(discord_post(
            url, content=f"{header}\nDownload{extra}: {link}"))

    limit = int(MAX_UPLOAD_MB * 1024 * 1024)

    if size <= limit - UPLOAD_MARGIN_BYTES:
        print(f"\nEnviando {zip_file.name} inteiro "
              f"({size / 1024 / 1024:.1f} MB)...")
        status = discord_post(url, content=header, file_path=zip_file)
        if is_ok(status):
            return True
        if status != 413:
            return False
        print(f"  Recusado pelo servidor (413). Reenviando dividido em "
              f"partes de {FALLBACK_UPLOAD_MB} MB...")
        limit = int(FALLBACK_UPLOAD_MB * 1024 * 1024)

    chunk = int(limit - UPLOAD_MARGIN_BYTES)
    total_parts = (size + chunk - 1) // chunk
    print(f"\nDividindo o zip em {total_parts} partes de ate "
          f"{chunk / 1024 / 1024:.1f} MB...")
    parts = split_file(zip_file, chunk)

    ok = is_ok(discord_post(url, content=(
        header + f"\nEnviado em {len(parts)} partes. Para juntar no Windows:\n"
        f"```\ncopy /b {zip_file.name}.001+{zip_file.name}.002+... "
        f"{zip_file.name}\n```")))

    for i, part in enumerate(parts, 1):
        print(f"  parte {i}/{len(parts)}: {part.name} "
              f"({part.stat().st_size / 1024 / 1024:.1f} MB)")
        if not is_ok(discord_post(url, file_path=part)):
            ok = False
            break

    if not keep_parts:
        for part in parts:
            part.unlink(missing_ok=True)

    return ok


# ------------------------------------------------------------------
# Menu
# ------------------------------------------------------------------

def escolher_acao():
    """Janelinha com as opcoes. Cai para menu de texto sem tkinter."""
    try:
        import tkinter as tk
    except ImportError:
        print("\n1 - Enviar (copiar, zipar e postar no Discord)")
        print("2 - Gerar o zip so local (nao envia nada)")
        print("3 - Apagar as mensagens que o script postou no canal")
        print("0 - Sair")
        return {"1": "upload", "2": "local", "3": "limpar"}.get(
            input("Opcao: ").strip(), "")

    escolha = {"valor": ""}
    root = tk.Tk()
    root.title("BT SPR/DAT")
    root.resizable(False, False)
    root.configure(padx=18, pady=16)

    def marcar(valor):
        escolha["valor"] = valor
        root.destroy()

    tk.Label(root, text="O que voce quer fazer?",
             font=("Segoe UI", 11, "bold")).pack(pady=(0, 12))

    tk.Button(root, text="Enviar SPR/DAT",
              command=lambda: marcar("upload"),
              width=34, height=2).pack(pady=4)
    tk.Label(root, text="copia, zipa e posta o link no canal",
             fg="#666", font=("Segoe UI", 8)).pack()

    tk.Button(root, text="Gerar zip local",
              command=lambda: marcar("local"),
              width=34, height=2).pack(pady=(12, 4))
    tk.Label(root, text="copia e zipa, sem enviar ao Discord",
             fg="#666", font=("Segoe UI", 8)).pack()

    tk.Button(root, text="Apagar mensagens do canal",
              command=lambda: marcar("limpar"),
              width=34, height=2).pack(pady=(12, 4))
    tk.Label(root, text="remove o que este script postou",
             fg="#666", font=("Segoe UI", 8)).pack()

    root.bind("<Escape>", lambda e: root.destroy())
    root.eval("tk::PlaceWindow . center")
    root.mainloop()
    return escolha["valor"]


def sync_dir(src_dir, dst_dir):
    """Espelha src_dir em dst_dir: copia o que mudou e apaga o que sobrou.

    Copiar por cima nao basta: quando o numero de fragmentos diminui, os
    Tibia_N.spr antigos ficariam no destino e entrariam no zip como lixo.
    """
    dst_dir.mkdir(parents=True, exist_ok=True)

    esperados = set()
    copiados = 0
    for src in sorted(src_dir.rglob("*")):
        rel = src.relative_to(src_dir)
        dst = dst_dir / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            esperados.add(rel)
            continue
        esperados.add(rel)
        # mtime + tamanho iguais = mesmo arquivo; poupa reescrever 1 GB
        # quando so alguns fragmentos mudaram.
        if dst.exists():
            s, d = src.stat(), dst.stat()
            if s.st_size == d.st_size and int(s.st_mtime) == int(d.st_mtime):
                continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copiados += 1

    removidos = 0
    for item in sorted(dst_dir.rglob("*"), reverse=True):
        rel = item.relative_to(dst_dir)
        if rel in esperados:
            continue
        if item.is_dir():
            shutil.rmtree(item, ignore_errors=True)
        else:
            item.unlink(missing_ok=True)
        removidos += 1

    total = sum(1 for r in esperados if (dst_dir / r).is_file())
    print(f"Sincronizado: {dst_dir.name}/ ({total} arquivos, "
          f"{copiados} atualizados, {removidos} removidos)")


def limpar_spr_monolitico():
    """Remove o Tibia.spr antigo do destino quando o set virou fragmentado.

    O monolitico tem ~1 GB; deixado para tras ele dobraria o tamanho do zip e
    o RME ainda daria preferencia ao .cat, entao nunca seria usado.
    """
    if not any(f.suffix.lower() == ".cat" for f in src_files):
        return
    for antigo in dest.glob("*.spr"):
        tamanho = antigo.stat().st_size / 1024 / 1024
        antigo.unlink(missing_ok=True)
        print(f"Removido (obsoleto): {antigo.name} ({tamanho:.1f} MB)")


def preparar_pacote():
    """Copia os arquivos e gera o zip. Devolve o caminho do zip."""
    dest.mkdir(parents=True, exist_ok=True)

    for f in src_files:
        if f.exists():
            shutil.copy2(f, dest / f.name)
            print(f"Copiado: {f.name}")
        else:
            print(f"Nao encontrado: {f}")

    for src_dir, nome in src_dirs:
        if src_dir.is_dir():
            sync_dir(src_dir, dest / nome)
        else:
            print(f"Nao encontrado: {src_dir}")

    limpar_spr_monolitico()

    for src, dst_dir in extra_files:
        dst_dir.mkdir(parents=True, exist_ok=True)
        if src.exists():
            shutil.copy2(src, dst_dir / src.name)
            print(f"Copiado: {src.name}")
        else:
            print(f"Nao encontrado: {src}")

    # Zipar com barra de progresso. rglob para pegar tambem os fragmentos
    # dentro de sprites/; o arcname mantem o caminho relativo, senao o .cat
    # nao acha os Tibia_N.spr depois de extrair.
    files_to_zip = [(f, f.relative_to(dest).as_posix())
                    for f in sorted(dest.rglob("*")) if f.is_file()]
    for src, dst_dir in extra_files:
        copied = dst_dir / src.name
        if copied.exists():
            files_to_zip.append((copied, copied.name))
    total_size = sum(f.stat().st_size for f, _ in files_to_zip)
    written = 0

    print(f"\nZipando {len(files_to_zip)} arquivos, "
          f"{total_size / 1024 / 1024:.1f} MB...")

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED,
                         allowZip64=True) as zf:
        for f, arcname in files_to_zip:
            size = f.stat().st_size
            zf.write(f, arcname)
            written += size
            pct = written * 100 // total_size if total_size else 100
            bar = "#" * (pct // 2) + "-" * (50 - pct // 2)
            sys.stdout.write(f"\r[{bar}] {pct}% - {arcname[:40]:<40}")
            sys.stdout.flush()

    print(f"\n\nZip criado: {zip_path} "
          f"({zip_path.stat().st_size / 1024 / 1024:.1f} MB)")
    return zip_path


# ------------------------------------------------------------------
# Main
# ------------------------------------------------------------------

parser = argparse.ArgumentParser(
    description="Copia os arquivos do client/server, zipa e envia ao Discord.")
parser.add_argument("--upload", action="store_true",
                    help="pula o menu e envia direto")
parser.add_argument("--clean", action="store_true",
                    help="pula o menu e apaga as mensagens do canal")
parser.add_argument("--dry-run", action="store_true",
                    help="com --clean: so lista o que seria apagado")
parser.add_argument("--no-discord", "--local", dest="no_discord",
                    action="store_true",
                    help="so copia e zipa, nao envia nada")
parser.add_argument("--link", action="store_true",
                    help="sobe o zip a um host externo e posta so o link")
parser.add_argument("--attach", action="store_true",
                    help="anexa no Discord (divide em partes se passar do limite)")
parser.add_argument("--host", choices=["gofile", "litterbox"],
                    help="host do upload externo (padrao: %s)" % UPLOAD_HOST)
parser.add_argument("--notify-only", action="store_true",
                    help="posta so a mensagem no canal, sem anexar o zip")
parser.add_argument("--keep-parts", action="store_true",
                    help="mantem os arquivos .001/.002 depois do envio")
parser.add_argument("--no-pause", action="store_true",
                    help="nao espera Enter no final")
args = parser.parse_args()

if args.link:
    DELIVERY = "link"
elif args.attach:
    DELIVERY = "attach"
if args.host:
    UPLOAD_HOST = args.host

if args.no_discord:
    acao = "local"           # so copia e zipa, sem tocar no Discord
elif args.upload:
    acao = "upload"
elif args.clean:
    acao = "limpar"
else:
    acao = escolher_acao()

# O modo local nao fala com o Discord, entao nao exige webhook configurado.
webhook_url = load_webhook_url()
if acao not in ("", "local") and not webhook_url:
    print("\nWebhook nao configurado: defina DISCORD_WEBHOOK_URL ou crie "
          f"{webhook_file.name} com a URL do webhook.")
    acao = ""

if acao == "limpar":
    limpar_canal(webhook_url, dry_run=args.dry_run)

elif acao == "local":
    preparar_pacote()
    print(f"\nPronto, so local: {zip_path}")

elif acao == "upload":
    preparar_pacote()
    if send_to_discord(webhook_url, zip_path, args.notify_only,
                       args.keep_parts):
        print("Enviado ao Discord.")
    else:
        print("Envio ao Discord falhou.")

else:
    print("\nNada a fazer.")

if not args.no_pause:
    input("\nPressione Enter para fechar...")
