# 🚀 GUIDA INSTALLAZIONE PARTICIPATION SYSTEM SU SERVER
## Mevacoin Full Node Incentive System

---

## 📋 PREREQUISITI

Prima di iniziare, verifica che hai:

```bash
# 1. Accesso root/sudo al server
sudo whoami  # Deve restituire "root"

# 2. Mevacoin clonato in /root/mevacoin
ls -la /root/mevacoin/src/
ls -la /root/mevacoin/CMakeLists.txt

# 3. Git installato
git --version

# 4. Compiler tool disponibili
gcc --version || clang --version
cmake --version
make --version
```

Se manca qualcosa, installa:
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    liblmdb-dev
```

---

## 📂 STRUTTURA DIRECTORY FINALE

Dopo l'installazione, avrai questa struttura:

```
/root/mevacoin/
├── src/
│   ├── participation/                    [NEW]
│   │   ├── node_registry.h              [NEW]
│   │   ├── node_registry.cpp            [NEW]
│   │   ├── participation_engine.h       [NEW]
│   │   ├── participation_engine.cpp     [NEW]
│   │   ├── availability_proof.h         [NEW]
│   │   ├── availability_proof.cpp       [NEW]
│   │   ├── badge_system.h               [NEW]
│   │   ├── badge_system.cpp             [NEW]
│   │   ├── reward_distributor.h         [NEW]
│   │   ├── reward_distributor.cpp       [NEW]
│   │   └── db/                          [NEW] (for future DB impl)
│   ├── rpc/
│   │   ├── participation_rpc_commands.h [NEW]
│   │   └── core_rpc_server_commands_defs.h [MODIFIED]
│   └── CMakeLists.txt                   [MODIFIED]
├── docs/
│   └── participation/                   [NEW]
│       ├── PARTICIPATION_SYSTEM.md      [NEW]
│       ├── IMPLEMENTATION_GUIDE.md      [NEW]
│       └── BLOCKCHAIN_MODIFICATIONS.md  [NEW]
└── participation.conf                   [NEW]

/var/lib/mevacoin/                       [NEW]
├── node_registry/                       [NEW]
├── participation/                       [NEW]
├── badges/                              [NEW]
└── rewards/                             [NEW]

/etc/systemd/system/
└── mevacoind-participation.service      [NEW]
```

---

## ⚡ PROCEDURA DI INSTALLAZIONE RAPIDA

### STEP 0: Preparare i file

Sul tuo computer locale, raccogli tutti i file generati:

```bash
# Cartella con tutti i file
mkdir -p ~/mevacoin_installation
cd ~/mevacoin_installation

# Copia da /tmp/ oppure dall'output di Claude
cp /tmp/*.h .
cp /tmp/*.cpp .
cp /tmp/*.md .
cp /tmp/*.service .
cp /tmp/install_participation_system.sh .

# Verifica di avere tutto
ls -la
```

### STEP 1: Trasferisci i file al server

```bash
# Dal tuo computer locale
scp -r ~/mevacoin_installation/* root@your_server_ip:/tmp/

# Verifica sul server
ssh root@your_server_ip "ls -la /tmp/*.h /tmp/*.service"
```

### STEP 2: Esegui lo script di installazione

```bash
# SSH sul server
ssh root@your_server_ip

# Naviga al directory /tmp
cd /tmp

# Rendi executable lo script
chmod +x install_participation_system.sh

# Esegui con sudo
sudo bash install_participation_system.sh 2>&1 | tee installation.log

# Il processo impiega 2-5 minuti
```

### STEP 3: Verifica l'installazione

```bash
# Controlla che i file siano stati creati
ls -la /root/mevacoin/src/participation/
ls -la /var/lib/mevacoin/

# Controlla il log di installazione
cat /tmp/participation_integration.log

# Leggi le istruzioni post-installazione
cat /tmp/NEXT_STEPS.txt
```

---

## 🔍 DETTAGLI PASSO-PASSO

### PASSO 1: File su /tmp

I file devono essere trasferiti nel server in `/tmp/`. Lo script li copia automaticamente da lì.

File richiesti:
```
/tmp/node_registry.h
/tmp/participation_engine.h
/tmp/availability_proof.h
/tmp/badge_system.h
/tmp/reward_distributor.h
/tmp/participation_rpc_commands.h
/tmp/mevacoind_participation.service
/tmp/mevacoin_incentives_analysis.md
/tmp/implementation_guide.md
/tmp/blockchain_modifications.cpp
/tmp/install_participation_system.sh
```

**Verificare**:
```bash
ls -la /tmp/*.h /tmp/*.service /tmp/*.md /tmp/*.sh
```

### PASSO 2: Esecuzione dello script

Lo script:

1. ✅ Crea le directory necessarie
2. ✅ Copia i file header in `src/participation/`
3. ✅ Crea file .cpp stub (placeholder)
4. ✅ Copia RPC commands in `src/rpc/`
5. ✅ Modifica `src/CMakeLists.txt` per linkare participation
6. ✅ Installa systemd service
7. ✅ Crea database directories in `/var/lib/mevacoin/`
8. ✅ Crea `participation.conf`
9. ✅ Copia documentazione in `docs/participation/`
10. ✅ Crea backup di file modificati

**Output atteso**:
```
[✓] Found: /root/mevacoin
[✓] Found: /root/mevacoin/src
[✓] Created: src/participation
[✓] Copied: node_registry.h → src/participation/
[✓] Copied: participation_engine.h → src/participation/
...
[✓] Installation complete!
```

### PASSO 3: Verifiche post-installazione

Dopo che lo script ha completato:

```bash
# 1. Controlla i file header
ls -la /root/mevacoin/src/participation/*.h

# 2. Controlla i file .cpp stub
ls -la /root/mevacoin/src/participation/*.cpp

# 3. Controlla RPC integration
grep -l "participation_rpc_commands" /root/mevacoin/src/rpc/*.h

# 4. Controlla database directories
ls -la /var/lib/mevacoin/

# 5. Controlla systemd service
ls -la /etc/systemd/system/mevacoind-participation.service

# 6. Controlla documentazione
ls -la /root/mevacoin/docs/participation/
```

---

## 📝 COSA SUCCEDE DURANTE L'INSTALLAZIONE

### File creati automaticamente:

#### 1. Header files (copiati da /tmp/)
```
/root/mevacoin/src/participation/
├── node_registry.h                  ← Registrazione nodi
├── participation_engine.h           ← Motore scoring
├── availability_proof.h             ← PoA challenge/response
├── badge_system.h                   ← Sistema badge
└── reward_distributor.h             ← Distribuzione premi
```

#### 2. File stub .cpp (creati dallo script)
```
/root/mevacoin/src/participation/
├── node_registry.cpp                ← Placeholder
├── participation_engine.cpp         ← Placeholder
├── availability_proof.cpp           ← Placeholder
├── badge_system.cpp                 ← Placeholder
└── reward_distributor.cpp           ← Placeholder
```

> **IMPORTANTE**: I file .cpp sono placeholder! Devi completare l'implementazione seguendo le specifiche nei .h file.

#### 3. RPC Integration (copia + modifica)
```
/root/mevacoin/src/rpc/
├── participation_rpc_commands.h     ← Copiato da /tmp/
└── core_rpc_server_commands_defs.h  ← MODIFICATO (aggiunto include)
```

#### 4. CMakeLists.txt (modificato)
```
/root/mevacoin/src/CMakeLists.txt    ← Aggiunto participation sources
```

#### 5. Database directories (creati)
```
/var/lib/mevacoin/
├── node_registry/
├── participation/
├── badges/
└── rewards/
```

#### 6. Systemd service (copiato)
```
/etc/systemd/system/mevacoind-participation.service
```

#### 7. Documentazione (copiata)
```
/root/mevacoin/docs/participation/
├── PARTICIPATION_SYSTEM.md
├── IMPLEMENTATION_GUIDE.md
└── BLOCKCHAIN_MODIFICATIONS.md
```

---

## 🔄 BACKUP AUTOMATICO

Lo script crea automaticamente un backup di tutti i file modificati:

```
/root/mevacoin/.participation_backup_20240601_153045/
├── CMakeLists.txt.bak
├── core_rpc_server_commands_defs.h.bak
└── mevacoind.service.bak
```

Se qualcosa va storto, puoi ripristinare:

```bash
# Vedi il timestamp del backup
ls -la /root/mevacoin/.participation_backup_*/

# Ripristina manualmente se necessario
cp /root/mevacoin/.participation_backup_XXXXXX/CMakeLists.txt.bak /root/mevacoin/src/CMakeLists.txt
```

---

## 📖 PROSSIMI STEP DOPO L'INSTALLAZIONE

Lo script genererà un file `NEXT_STEPS.txt` che dice esattamente cosa fare:

```bash
cat /tmp/NEXT_STEPS.txt
```

**Fasi principali**:

1. **IMPLEMENTAZIONE** (Weeks 1-6)
   - Completare i .cpp file
   - Integrare RPC handlers
   - Integrare blockchain.cpp
   - Aggiungere P2P protocol

2. **COMPILAZIONE** (Week 6-7)
   ```bash
   cd /root/mevacoin
   mkdir build
   cd build
   cmake ..
   make -j$(nproc)
   ```

3. **TESTING** (Week 7-8)
   - Unit tests
   - Integration tests
   - Testnet deployment

4. **LAUNCH** (Week 8+)
   - Security audit
   - Mainnet soft-fork
   - Monitoring

---

## ⚠️ TROUBLESHOOTING

### Script fails con "permission denied"

**Soluzione**: Esegui con sudo
```bash
sudo bash /tmp/install_participation_system.sh
```

### Directory /var/lib/mevacoin non creata

**Soluzione**: Crea manualmente
```bash
sudo mkdir -p /var/lib/mevacoin/{node_registry,participation,badges,rewards}
sudo chmod 755 /var/lib/mevacoin/*
```

### File headers non trovati

**Soluzione**: Verifica che siano in /tmp/
```bash
ls -la /tmp/*.h
# Se mancano, copiali di nuovo dal tuo computer
scp ~/mevacoin_installation/*.h root@server:/tmp/
```

### CMakeLists.txt modification failed

**Soluzione**: Aggiungi manualmente
```bash
cat >> /root/mevacoin/src/CMakeLists.txt << 'EOF'

set(participation_sources
  participation/node_registry.cpp
  participation/participation_engine.cpp
  participation/availability_proof.cpp
  participation/badge_system.cpp
  participation/reward_distributor.cpp
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/participation)
EOF
```

### Systemd service won't load

**Soluzione**: Controlla la sintassi
```bash
systemctl daemon-reload
systemctl status mevacoind-participation.service
journalctl -u mevacoind-participation.service -n 20
```

---

## 🛠️ CONFIGURAZIONE POST-INSTALLAZIONE

Dopo l'installazione, modifica `/root/mevacoin/participation.conf` per tuo ambiente:

```bash
nano /root/mevacoin/participation.conf
```

**Parametri importanti**:

```ini
# Abilita il sistema
enable-participation=1

# Percentuale reward (3% consigliato)
pool-percentage=0.03

# Database paths
node-registry-db=/var/lib/mevacoin/node_registry
participation-db=/var/lib/mevacoin/participation

# Port per participation RPC (separate da main RPC)
participation-port=18088

# PoA parameters
poa-challenge-interval=36000    # 10 hours
poa-timeout-ms=2000            # 2 seconds
```

---

## 📊 VERIFICHE FINALI

Dopo l'installazione, esegui queste verifiche:

```bash
#!/bin/bash
# Verification script

echo "=== PARTICIPATION SYSTEM VERIFICATION ==="

echo ""
echo "1. Header files:"
ls -lh /root/mevacoin/src/participation/*.h | wc -l
echo "   Atteso: 5 file"

echo ""
echo "2. C++ files:"
ls -lh /root/mevacoin/src/participation/*.cpp | wc -l
echo "   Atteso: 5 file (stub)"

echo ""
echo "3. RPC commands:"
[ -f /root/mevacoin/src/rpc/participation_rpc_commands.h ] && echo "✓ Found" || echo "✗ Missing"

echo ""
echo "4. Database directories:"
du -sh /var/lib/mevacoin/*

echo ""
echo "5. Systemd service:"
systemctl list-unit-files | grep participation

echo ""
echo "6. Documentation:"
ls -lh /root/mevacoin/docs/participation/ | tail -n +2 | wc -l
echo "   Atteso: 3 file"

echo ""
echo "7. Configuration:"
[ -f /root/mevacoin/participation.conf ] && echo "✓ Found" || echo "✗ Missing"

echo ""
echo "=== VERIFICATION COMPLETE ==="
```

---

## 🚀 PRONTO PER IMPLEMENTAZIONE

Dopo l'installazione, sei pronto per:

1. ✅ Completare l'implementazione dei .cpp file
2. ✅ Integrare i RPC handlers
3. ✅ Modificare blockchain.cpp
4. ✅ Aggiungere P2P protocol
5. ✅ Testare su testnet
6. ✅ Deploy su mainnet

Tutti i file, la documentazione, e gli strumenti sono in place!

---

## 📞 SUPPORT

Se hai problemi:

1. Controlla il log di installazione:
   ```bash
   cat /tmp/participation_integration.log
   ```

2. Leggi le istruzioni post-installazione:
   ```bash
   cat /tmp/NEXT_STEPS.txt
   ```

3. Consulta la documentazione:
   ```bash
   cat /root/mevacoin/docs/participation/PARTICIPATION_SYSTEM.md
   ```

---

**Status**: ✅ Ready for Implementation

Buona fortuna con l'installazione! 🎉

