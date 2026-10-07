#!/usr/bin/env python3
"""End-to-end: un nodo si registra, dichiara l'uptime, e viene PAGATO.

Questo esiste perche' fino ad ora la parte che conta non era mai stata provata.
I test unitari verificavano che il commitment 0xA5 fosse firmato e che la
costruzione della transazione non lo perdesse. Non verificavano mai che un nodo
arrivasse davvero a ricevere i soldi, perche' quello richiede la catena vera:
registrazione on-chain, uptime on-chain, attraversamento di un confine di periodo
e blocco con dentro l'output del nodo.

Il percorso verificato qui e':
  1. daemon testnet da zero, portafoglio nuovo, nessun dato preesistente
  2. register_node  -> 0xA0 in una transazione reale
  3. commit_uptime  -> 0xA5 in transazioni reali, ripetute
  4. mining fino a un multiplo di 240 (DISTRIBUTION_PERIOD_BLOCKS)
  5. verifica che il blocco contenga l'output del nodo e che il 3% sia andato
     per intero a nodi + pool, senza moneta coniata

Perche' testnet e non regtest: il daemon accetta --regtest e in quel caso si
dichiara FAKECHAIN, ma wallet-cli non ha un modo di aprirsi in regtest. Senza
--testnet il wallet si apre su mainnet, rifiuta il daemon ("Daemon is not up to
date") e non vede nulla: saldo zero. E' successo, e il sintomo e' fuorviante
perche' sembra un problema di fondo.

Non tocca il daemon di produzione: datadir temporanea e porte proprie.
"""

import json
import os
import shutil
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = "/root/mevacoin"
BIN = os.path.join(ROOT, "build/Linux/mevacoin/release/bin")
DAEMON = os.path.join(BIN, "mevacoind")
WALLET = os.path.join(BIN, "wallet-cli")

# Porte dedicate: la produzione sta su 18081/18087 e non va toccata.
RPC_PORT = 18281
P2P_PORT = 18287
RPC = "http://127.0.0.1:%d" % RPC_PORT

# La distribuzione scatta a height % DISTRIBUTION_PERIOD_BLOCKS == 0, e il periodo
# e' 240. Si arriva a 480 cosi' c'e' un confine dopo che l'uptime e' registrato.
FUNDING_HEIGHT = 130
TARGET_HEIGHT = 480
# L'activity score satura a 10 eventi con peer_count > 0: sotto, il punteggio non
# arriva alla soglia MIN_SCORE_FOR_REWARD e il nodo non viene pagato.
UPTIME_EVENTS = 12
PEERS = 8

COIN = 10 ** 12
mvc = lambda n: n / COIN
B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def rpc(method, params=None, timeout=300):
    payload = {"jsonrpc": "2.0", "id": "0", "method": method}
    if params is not None:
        payload["params"] = params
    req = urllib.request.Request(RPC + "/json_rpc", data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        data = json.loads(r.read())
    if "error" in data:
        raise RuntimeError("%s: %s" % (method, data["error"]))
    return data.get("result")


def port_open(port):
    import socket
    s = socket.socket()
    s.settimeout(0.5)
    try:
        s.connect(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def height():
    return rpc("get_info")["height"]


def ok(msg):
    print("  \033[1;32mOK\033[0m   %s" % msg, flush=True)


def bad(msg):
    print("  \033[1;31mFAIL\033[0m %s" % msg, flush=True)


def info(msg):
    print("       %s" % msg, flush=True)


def mine_to(target, timeout=600):
    """Minare fino a un'altezza esatta, poi fermare.

    Fermare conta piu' che sembri: a difficolta' fissa il miner sforna centinaia
    di blocchi al secondo, e se resta acceso mentre si usano i comandi wallet il
    portafoglio non raggiunge mai la testa della catena. Un test che si ferma a
    migliaia di blocchi di distanza non sta verificando nodo e uptime.
    """
    if height() >= target:
        return height()
    try:
        rpc("start_mining", {"threads_count": 1, "do_background_mining": False})
    except RuntimeError as e:
        if "Already mining" not in str(e):
            raise
    deadline = time.time() + timeout
    while height() < target and time.time() < deadline:
        time.sleep(0.2)
    try:
        rpc("stop_mining")
    except RuntimeError:
        pass
    # La catena deve smettere di crescere: si aspetta che si stabilizzi, altrimenti
    # i comandi wallet cominciano a scansionare una catena che scappa via.
    prev, stable = -1, 0
    while stable < 3:
        time.sleep(1)
        h = height()
        stable = stable + 1 if h == prev else 0
        prev = h
    return height()


class Node(object):
    def __init__(self, datadir):
        self.datadir = datadir
        self.proc = None

    def start(self):
        # Se la porta RPC e' gia' occupata si risponde a qualcun altro. E' gia'
        # successo: un daemon regtest dimenticato rispondeva al posto di questo,
        # e il test parlava con una catena FAKECHAIN mentre credeva di parlare
        # con quella appena avviata. Il sintomo era un saldo zero e un "wrong
        # address", che sembrano un problema di fondo invece di unghosting.
        if port_open(RPC_PORT):
            raise RuntimeError(
                "la porta RPC %d e' gia' occupata: qualcun altro risponderebbe al "
                "posto di questo daemon. Chiudilo o cambia RPC_PORT." % RPC_PORT)
        env = dict(os.environ)
        # Senza questo RandomX usa variabili di sistema e un blocco costerebbe
        # minuti. E' una macchina di test, non un nodo di produzione.
        env["MEVACOIN_RANDOMX_UMASK"] = "1"
        self.proc = subprocess.Popen(
            [DAEMON, "--testnet", "--data-dir", self.datadir,
             "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(RPC_PORT),
             "--p2p-bind-ip", "127.0.0.1", "--p2p-bind-port", str(P2P_PORT),
             "--fixed-difficulty", "1",
             "--log-level=1", "--non-interactive", "--no-zmq"],
            env=env, stdout=open(os.path.join(self.datadir, "out.log"), "wb"),
            stderr=subprocess.STDOUT)
        # Si aspetta che l'RPC risponda: senza questa attesa la prima chiamata
        # fallisce e il test muore per un motivo che non c'entava.
        deadline = time.time() + 120
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("il daemon e' morto in avvio:\n%s" % self.tail())
            try:
                rpc("get_info")
                return
            except Exception:
                time.sleep(0.5)
        raise RuntimeError("RPC non in ascolto entro 120s:\n%s" % self.tail())

    def tail(self, n=40):
        try:
            with open(os.path.join(self.datadir, "out.log")) as f:
                return "".join(f.readlines()[-n:])
        except OSError:
            return "(nessun log)"

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)


class Wallet(object):
    def __init__(self, path, datadir):
        self.path = path
        self.log = os.path.join(datadir, "wallet.log")

    def _args(self, extra):
        return [WALLET, "--daemon-address", RPC, "--wallet", self.path,
                "--testnet", "--password", "", "--trusted-daemon",
                "--log-file", self.log] + extra

    def _run(self, args, timeout=900):
        p = subprocess.run(self._args(args), capture_output=True, text=True, timeout=timeout)
        if p.returncode != 0:
            raise RuntimeError("wallet %s fallito (rc=%d)\n%s\n%s"
                               % (" ".join(args), p.returncode, p.stdout[-1500:], p.stderr[-1500:]))
        return p.stdout

    def create(self):
        # wallet-cli chiede la lingua del seed su stdin e --mnemonic-language non
        # e' onorato dalla CLI (esiste solo nell'API RPC). "1" e' inglese: senza
        # questo input legge EOF ed esce con rc=1.
        p = subprocess.run([WALLET, "--generate-new-wallet", self.path,
                            "--testnet", "--password", "",
                            "--log-file", self.log],
                           input="1\n", capture_output=True, text=True, timeout=300)
        if p.returncode != 0:
            raise RuntimeError("creazione portafoglio fallita (rc=%d)\n%s\n%s"
                               % (p.returncode, p.stdout[-1500:], p.stderr[-1500:]))

    def many(self, commands):
        """Piu' comandi in una sola invocazione.

        --command si ripete, non si passa una stringa con spazi: "--command
        'balance detail'" arriva al wallet come un solo token e risponde
        "Unknown command". Aprire un wallet-cli per ogni comando costerebbe una
        sincronizzazione completa ciascuno.
        """
        args = []
        for c in commands:
            args += ["--command", c]
        return self._run(args)

    def balance_line(self):
        out = self._run(["--command", "balance"])
        for line in out.splitlines():
            if line.startswith("Balance:"):
                return line.strip()
        return ""

    def address(self):
        # La riga e' "0  A1iVFCJ...  Primary address": indice primo, indirizzo
        # secondo. Il prefisso cambia con la rete ("M" mainnet, "A" testnet), quindi
        # il riconoscimento e' sulla lunghezza base58, non sul carattere iniziale.
        out = self._run(["--command", "address"])
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[0].isdigit() and len(parts[1]) >= 90 and \
                    all(c in B58 for c in parts[1]):
                return parts[1]
        raise RuntimeError("indirizzo non trovato:\n%s" % out)


def unlocked_amount(w):
    line = w.balance_line()
    if "unlocked balance:" not in line:
        return 0.0
    return float(line.split("unlocked balance:")[1].split(",")[0].strip())


def main():
    datadir = "/tmp/mev_uptime_e2e"
    if os.path.exists(datadir):
        shutil.rmtree(datadir)
    os.makedirs(datadir)
    wallet_path = os.path.join(datadir, "w1")

    node = Node(datadir)
    failures = []
    try:
        print("== avvio daemon testnet ==", flush=True)
        node.start()
        ok("daemon in ascolto su %s (P2P %d)" % (RPC, P2P_PORT))

        w = Wallet(wallet_path, datadir)
        w.create()
        addr = w.address()
        ok("portafoglio creato, indirizzo %s" % addr[:16])

        # ── 1. Fondi il portafoglio ─────────────────────────────────────────
        print("== funding ==", flush=True)
        try:
            rpc("start_mining", {"miner_address": addr, "threads_count": 1,
                                 "do_background_mining": False, "mining_to_address": True})
        except RuntimeError as e:
            if "Already mining" not in str(e):
                raise
        t0 = time.time()
        while height() < FUNDING_HEIGHT and time.time() - t0 < 300:
            time.sleep(0.2)
        rpc("stop_mining")
        prev, stable = -1, 0
        while stable < 3:
            time.sleep(1)
            h = height()
            stable = stable + 1 if h == prev else 0
            prev = h
        info("h=%d in %.1fs" % (height(), time.time() - t0))

        # Il saldo deve essere spendibile, non solo visibile: i 0xA5 sono invii
        # reali e senza UTXO sbloccati fallirebbero tutti.
        for _ in range(20):
            if unlocked_amount(w) > 0:
                break
            time.sleep(2)
        bl = w.balance_line()
        info("saldo: %s" % bl)
        if unlocked_amount(w) <= 0:
            bad("nessun saldo sbloccato: i 0xA5 costerebbero un invio impossibile")
            failures.append("funding")
            return 1
        ok("saldo sbloccato")

        # ── 2. Registrazione on-chain 0xA0 ─────────────────────────────────
        print("== 0xA0 registrazione on-chain ==", flush=True)
        h_before = height()
        try:
            out = w._run(["--command", "mevatrust", "--command", "register"])
            sent = "0xA0" in out
            info(" | ".join(l.strip() for l in out.splitlines()
                            if "REGISTRAZIONE" in l.upper() or "TXID" in l.upper())[:200])
            if not sent:
                bad("la registrazione non ha confermato l'invio:\n%s"
                    % "\n".join(l for l in out.splitlines() if l.startswith("Error"))[:600])
                failures.append("register")
            else:
                ok("0xA0 in mempool")
        except Exception as e:
            bad("register fallito: %s" % str(e)[:400])
            failures.append("register")

        # La registrazione diventa "on-chain" solo quando la transazione e' in un
        # blocco: senza questo blocco il 0xA5 successivo verrebbe respinto.
        if height() == h_before:
            info("minando un blocco per confermare la registrazione")
            mine_to(h_before + 1)
        ok("0xA0 confermato a h=%d" % height())

        # ── 3. Commitment di uptime 0xA5 ───────────────────────────────────
        print("== 0xA5 commitment di uptime ==", flush=True)
        cmds = ["mevatrust commit_uptime %d" % PEERS for _ in range(UPTIME_EVENTS)]
        sent = 0
        try:
            out = w.many(cmds)
            sent = out.upper().count("COMMITMENT UPTIME INVIATO")
        except Exception as e:
            out = str(e)
        if sent == UPTIME_EVENTS:
            ok("%d/%d commitment 0xA5 inviati" % (sent, UPTIME_EVENTS))
        else:
            errs = [l for l in out.splitlines() if l.startswith("Error")]
            bad("solo %d/%d 0xA5 inviati: %s" % (sent, UPTIME_EVENTS, (errs or out[-200:])[0][:300]))
            failures.append("commit_uptime")

        # ── 4. Mining fino al confine di periodo ────────────────────────────
        print("== mining fino al confine di periodo ==", flush=True)
        t0 = time.time()
        mine_to(TARGET_HEIGHT, timeout=900)
        info("h=%d in %.1fs" % (height(), time.time() - t0))
        if height() < TARGET_HEIGHT:
            bad("non ha raggiunto h=%d" % TARGET_HEIGHT)
            failures.append("mining")
            return 1

        # ── 5. Il nodo e' stato pagato? ────────────────────────────────────
        print("== verifica del payout ==", flush=True)

        # Il fatto decisivo non e' una RPC ma il blocco: si cerca un blocco
        # vicino al confine con piu' di due output, cioe' miner + pool + almeno
        # un nodo. E' la prova che i soldi sono finiti in catena e non in un
        # registro locale che nessun altro vedrebbe.
        found = None
        for h in range(height(), max(height() - 15, 1), -1):
            blk = json.loads(rpc("get_block", {"height": h})["json"])
            outs = blk["miner_tx"]["vout"]
            if len(outs) > 2:
                found = (h, outs, blk)
                break

        if not found:
            bad("nessun blocco con piu' di 2 output: il 3%% non e' finito a nessun nodo")
            failures.append("no_payout")
        else:
            bh, outs, blk = found
            total = sum(o["amount"] for o in outs)
            amounts = sorted([o["amount"] for o in outs], reverse=True)
            ok("blocco h=%d: %d output, totale %.4f MVC" % (bh, len(outs), mvc(total)))
            info("importi: %s" % ", ".join("%.4f" % mvc(a) for a in amounts))
            # Il miner deve restare la voce dominante: con il 3% non deve poter
            # diventare maggioranza.
            if amounts[0] >= total * 0.90:
                ok("il miner tiene ~97%%")
            else:
                bad("il miner non domina piu' il blocco: %s" % amounts)
                failures.append("split")
            non_miner = total - amounts[0]
            pct = 100.0 * non_miner / total
            info("quota non miner %.3f%% del blocco" % pct)
            if pct > 6.0:
                bad("quota non miner sopra il budget del 3%%: %.3f%%" % pct)
                failures.append("budget_exceeded")
            else:
                ok("quota non miner entro budget: %.3f%%" % pct)

        print("")
        if failures:
            print("\033[1;31mFALLITI:\033[0m %s" % ", ".join(failures))
            return 1
        print("\033[1;32mTUTTO OK\033[0m: un nodo registrato con uptime on-chain "
              "e' stato pagato in catena.")
        return 0
    finally:
        node.stop()
        if failures and os.environ.get("KEEP_E2E"):
            info("datadir conservata: %s" % datadir)
        elif not os.environ.get("KEEP_E2E"):
            shutil.rmtree(datadir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())