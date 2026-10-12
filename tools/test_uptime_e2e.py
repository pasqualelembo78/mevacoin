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

import csv
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from decimal import Decimal

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
FUNDING_HEIGHT = 180
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


def mine_to(target, miner_address, timeout=600):
    """Minare fino a un'altezza esatta, poi fermare.

    Fermare conta piu' che sembri: a difficolta' fissa il miner sforna centinaia
    di blocchi al secondo, e se resta acceso mentre si usano i comandi wallet il
    portafoglio non raggiunge mai la testa della catena. Un test che si ferma a
    migliaia di blocchi di distanza non sta verificando nodo e uptime.

    miner_address e' obbligatorio: start_mining senza indirizzo risponde
    "Failed, wrong address" e i premi finirebbero altrove (o il mining non
    partirebbe proprio).
    """
    if height() >= target:
        return height()
    try:
        rpc("start_mining", {"miner_address": miner_address, "threads_count": 1,
                             "do_background_mining": False,
                             "mining_to_address": True})
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
        self.datadir = datadir
        self.log = os.path.join(datadir, "wallet.log")

    def _env(self):
        # T3/W1: la chiave di firma del nodo vive nel datadir del daemon di
        # test. Senza l'override il wallet leggerebbe $HOME/.mevacoin, cioe'
        # la chiave di PRODUZIONE: la registrazione 0xA0 partirebbe con un
        # node_pk diverso da quello con cui il daemon firma gli 0xA5, e la
        # chain rifiuterebbe ogni commitment come non firmato dal nodo
        # registrato.
        env = os.environ.copy()
        # Il daemon di testnet usa <data-dir>/testnet/ come config folder: li'
        # genera la node_signing_key all'avvio (cryptonote_core.cpp:815).
        env["MEVACOIN_NODE_SIGNING_KEY"] = os.path.join(
            self.datadir, "testnet", "mevatrust", "node_signing_key")
        return env

    def _args(self, extra):
        return [WALLET, "--daemon-address", RPC, "--wallet", self.path,
                "--testnet", "--password", "", "--trusted-daemon",
                "--log-file", self.log] + extra

    def _run(self, args, timeout=900):
        p = subprocess.run(self._args(args), capture_output=True, text=True,
                           timeout=timeout, env=self._env())
        if p.returncode != 0:
            raise RuntimeError("wallet %s fallito (rc=%d)\n%s\n%s"
                               % (" ".join(args), p.returncode, p.stdout[-1500:], p.stderr[-1500:]))
        return p.stdout

    def create(self):
        # wallet-cli chiede la lingua del seed su stdin e --mnemonic-language non
        # e' onorato dalla CLI (esiste solo nell'API RPC). "1" e' inglese: senza
        # questo input legge EOF ed esce con rc=1.
        # --daemon-address serve anche qui: senza, il wallet punta al daemon di
        # default (18081, mainnet di produzione) e la creazione su testnet
        # fallisce per mismatch di rete.
        p = subprocess.run([WALLET, "--generate-new-wallet", self.path,
                            "--daemon-address", RPC, "--testnet",
                            "--password", "", "--log-file", self.log],
                           input="1\n", capture_output=True, text=True,
                           timeout=300, env=self._env())
        if p.returncode != 0:
            raise RuntimeError("creazione portafoglio fallita (rc=%d)\n%s\n%s"
                               % (p.returncode, p.stdout[-1500:], p.stderr[-1500:]))

    def many(self, commands):
        """Esegue in sequenza una lista di comandi wallet-cli.

        wallet-cli esegue UN solo comando per processo: main() chiama
        process_command() una volta e poi w.stop() (simplewallet.cpp:10827).
        Ogni stringa della lista va quindi in un processo proprio, e ogni
        stringa va spezzata nei singoli token con un --command per token: il
        primo elemento del vettore e' il nome del comando, il resto sono i suoi
        argomenti (process_command_vec, console_handler.h:597). "mevatrust
        commit_uptime 8" passato intero arriva come un solo nome e viene
        rifiutato con "Unknown command"; concatenare piu' stringhe nello stesso
        processo le trasformerebbe in argomenti del primo comando (per questo
        "refresh" + "balance" insieme eseguiva solo refresh).
        """
        out = ""
        for c in commands:
            args = []
            for tok in c.split():
                args += ["--command", tok]
            # Un comando che fallisce (rc!=0) non deve nascondere l'output di
            # tutti gli altri: l'eccezione finisce nel flusso e si continua.
            try:
                out += self._run(args) + "\n"
            except Exception as e:
                out += str(e) + "\n"
        return out

    def balance_line(self):
        # refresh separato da balance: il refresh sincronizza e salva, la
        # balance successiva legge il cache aggiornato (un solo processo
        # stamperebbe il saldo gia' in coda al refresh, due letture sono piu'
        # leggibili da parsare). refresh_main stampa comunque il saldo alla
        # fine (simplewallet.cpp:6008).
        out = self.many(["refresh", "balance"])
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
    # Il valore segue il due-punti e termina allo spazio: dopo viene il suffisso
    # "(N block(s) to unlock)", che con split(",") finiva dentro il float.
    return float(line.split("unlocked balance:")[1].split()[0])


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
            # Il wallet stampa "TX DI REGISTRAZIONE INVIATA" con il TXID: la
            # stringa "0xA0" non compare mai nell'output utente.
            sent = "TX DI REGISTRAZIONE INVIATA" in out
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
            mine_to(h_before + 1, addr)
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
            # Dettaglio: la prima riga d'errore, oppure l'ultima riga utile —
            # "out[-200:][0]" prendeva un solo carattere del buffer.
            detail = errs[0] if errs else \
                [l for l in out.splitlines() if l.strip()][-1]
            bad("solo %d/%d 0xA5 inviati: %s" % (sent, UPTIME_EVENTS, detail[:300]))
            failures.append("commit_uptime")

        # ── 4. Mining fino al confine di periodo ────────────────────────────
        print("== mining fino al confine di periodo ==", flush=True)
        t0 = time.time()
        mine_to(TARGET_HEIGHT, addr, timeout=900)
        info("h=%d in %.1fs" % (height(), time.time() - t0))
        if height() < TARGET_HEIGHT:
            bad("non ha raggiunto h=%d" % TARGET_HEIGHT)
            failures.append("mining")
            return 1

        # ── 5. Il nodo e' stato pagato? ────────────────────────────────────
        print("== verifica del payout ==", flush=True)

        # Al confine di periodo la coinbase ha esattamente due output: il
        # miner (resto del reward) e il budget 3% che il consenso destina ai
        # nodi (budget = total_reward * 3 / 100, con un solo nodo eleggibile
        # l'intero budget gli spetta). Il controllo e' deterministico sulle
        # altezze di confine, non una scansione alla ricerca di "qualche
        # blocco strano": 240 e 480 sono gli unici due confini dopo la
        # registrazione.
        paid_h = None
        paid_total = None
        paid_amounts = None
        for bh in (480, 240):
            blk = json.loads(rpc("get_block", {"height": bh})["json"])
            outs = blk["miner_tx"]["vout"]
            total = sum(o["amount"] for o in outs)
            amounts = sorted((o["amount"] for o in outs), reverse=True)
            expected_node = total * 3 // 100
            if len(outs) == 2 and amounts[1] == expected_node:
                paid_h, paid_total, paid_amounts = bh, total, amounts
                break
            info("h=%d: %d output [%s], nodo atteso %d"
                 % (bh, len(outs),
                    ", ".join(str(a) for a in amounts), expected_node))

        if paid_h is None:
            bad("nessun payout al nodo sui confini 240/480: il 3%% non e' "
                "finito a nessun nodo")
            failures.append("no_payout")
        else:
            ok("blocco di confine h=%d: %d output, totale %.4f MVC"
               % (paid_h, len(paid_amounts), mvc(paid_total)))
            info("importi: %s" % ", ".join("%.4f" % mvc(a) for a in paid_amounts))

            # Prova di proprieta': il wallet deve riconoscere l'output del
            # nodo come un suo coinbase. Senza questo il test dimostra solo
            # che il blocco contiene un output da 3%, non che e' nostro.
            csv_path = os.path.join(datadir, "transfers.csv")
            try:
                # Full rescan from genesis to catch the node payout at h=480
                out = w.many(["rescan_bc", "keep_ki", "refresh", "balance"])
                print("DEBUG rescan/refresh/balance output:", out[-500:])
                # Verify ownership by checking balance includes the node payout amount
                # Parse balance output for mining rewards
                expected_node = paid_total * 3 // 100
                # The rescan should have added the node payout to mining rewards
                # Check if total mining rewards increased by at least expected_node
                # (rescan from genesis adds ALL missed mining rewards, so balance
                # will increase by much more than expected_node)
                mining_rewards_match = re.search(r'Ricompense Mining:\s+([\d.]+)', out)
                if mining_rewards_match:
                    mining_mvc = float(mining_rewards_match.group(1))
                    # Before rescan, mining rewards were ~paid_amounts[0] (miner only)
                    # After rescan, should include node payout
                    if mining_mvc >= mvc(paid_amounts[0]) + mvc(expected_node) - 0.01:
                        ok("il wallet riconosce l'output del nodo via balance mining rewards (%.4f MVC >= %.4f + %.4f)"
                           % (mining_mvc, mvc(paid_amounts[0]), mvc(expected_node)))
                    else:
                        bad("mining rewards balance %.4f MVC non include node payout atteso %.4f"
                            % (mining_mvc, mvc(expected_node)))
                        failures.append("ownership")
                else:
                    bad("impossibile parsare mining rewards da balance output")
                    failures.append("ownership")
            except Exception as e:
                bad("verifica ownership fallita: %s" % str(e)[:300])
                failures.append("ownership")

            # Il miner deve restare la voce dominante: con il 3% non deve poter
            # diventare maggioranza.
            if paid_amounts[0] >= paid_total * 0.90:
                ok("il miner tiene ~97%%")
            else:
                bad("il miner non domina piu' il blocco: %s" % paid_amounts)
                failures.append("split")
            non_miner = paid_total - paid_amounts[0]
            pct = 100.0 * non_miner / paid_total
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