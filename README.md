# Mevacoin (MVC)

Copyright (c) 2024, Mevacoin Project  
Portions Copyright (c) 2014-2024, The Monero Project (BSD-3-Clause, adattato)

## Cos’è Mevacoin

Mevacoin è una criptovaluta **privata, sicura e decentralizzata**.  
- **Privacy:** le transazioni non sono facilmente tracciabili.  
- **Sicurezza:** ogni transazione è protetta crittograficamente.  
- **Decentralizzazione:** chiunque può partecipare alla rete con hardware standard.

## Risorse principali

- Sito web: [https://mevacoin.org](https://mevacoin.org)  
- GitHub: [https://github.com/pasqualelembo78/mevacoin](https://github.com/pasqualelembo78/mevacoin)  
- Supporto / domande: IRC `#mevacoin-dev` su Libera Chat  

## Compilare Mevacoin

### Dipendenze principali (Ubuntu/Debian)

```bash
sudo apt update && sudo apt install build-essential cmake pkg-config libssl-dev libzmq3-dev libsodium-dev libunwind8-dev liblzma-dev libreadline-dev libexpat1-dev qttools5-dev-tools libhidapi-dev libusb-1.0-0-dev libprotobuf-dev protobuf-compiler libudev-dev
apt update && apt install -y libunbound-dev
apt update && apt install -y libboost-all-dev
apt install -y ccache
````

### Clonare il repository

```bash
git clone --recursive https://github.com/pasqualelembo78/mevacoin
cd mevacoin
```

### Compilare

```bash
make
```

Gli eseguibili si trovano in `build/release/bin`.

## Eseguire il demone

```bash
./mevacoind --detach
```

## Licenza

BSD-3-Clause. Vedi [LICENSE](LICENSE).

---

**Nota:** Questo README è basato su Monero README (BSD-3-Clause) e adattato a Mevacoin.

```

--- faccia anche quella?
```
