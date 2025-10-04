#!/bin/bash

# Abilita uscita immediata in caso di errore
set -e

echo "Aggiornamento del sistema..."
sudo apt update && sudo apt upgrade -y

echo "Installazione pacchetti di base, Python, PHP, cURL e Apache..."
sudo apt update
sudo apt install -y build-essential cmake git ccache clang g++ \
python3 python3-pip python3-pyqt5 \
libssl-dev libzmq3-dev libsodium-dev libbz2-dev zlib1g-dev liblzma-dev \
libboost-all-dev apache2 ufw \
php php-cli php-curl curl

echo "Configurazione variabili d'ambiente Boost..."
{
  echo 'export BOOST_ROOT=/usr'
  echo 'export BOOST_INCLUDEDIR=/usr/include'
  echo 'export BOOST_LIBRARYDIR=/usr/lib/x86_64-linux-gnu'
} >> /root/.bashrc

# Carico subito le variabili anche per questa sessione
export BOOST_ROOT=/usr
export BOOST_INCLUDEDIR=/usr/include
export BOOST_LIBRARYDIR=/usr/lib/x86_64-linux-gnu

echo "Configurazione del firewall UFW..."
sudo ufw allow 22/tcp         # SSH
sudo ufw allow 21/tcp         # FTP
sudo ufw allow 4000/tcp       # NoMachine
sudo ufw allow 17080/tcp      # Mevacoin P2P
sudo ufw allow 17081/tcp      # Mevacoin RPC
sudo ufw allow 17082/tcp      # Mevacoin RPC secondario
sudo ufw allow 17083/tcp 
sudo ufw allow 17084/tcp 
sudo ufw allow 17085/tcp
sudo ufw allow 17086/tcp
sudo ufw allow 'Apache'       # HTTP (porta 80)
sudo ufw allow 'Apache Full'  # HTTP + HTTPS (porte 80 e 443)
sudo ufw --force enable

echo "Firewall configurato."

echo "Clonazione di Mevacoin in /opt/mevacoin..."
sudo rm -rf /opt/mevacoin
git clone https://github.com/pasqualelembo78/mevacoin.git /opt/mevacoin

echo "Compilazione..."
cd /opt/mevacoin
mkdir -p build && cd build
cmake ..
make -j$(nproc)

echo "Configurazione del servizio mevacoind..."
sudo tee /etc/systemd/system/mevacoind.service > /dev/null <<EOF
[Unit]
Description=Mevacoin Daemon
After=network.target

[Service]
ExecStart=/opt/mevacoin/build/src/mevacoind --rpc-bind-ip 0.0.0.0
WorkingDirectory=/opt/mevacoin/build/src
Restart=always
RestartSec=5
User=root
LimitNOFILE=4096

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl start mevacoind
sudo systemctl enable mevacoind

echo "Configurazione del servizio wallet-api..."
sudo tee /etc/systemd/system/wallet-api.service > /dev/null <<EOF
[Unit]
Description=Mevacoin Wallet API
After=network.target

[Service]
ExecStart=/usr/bin/screen -DmS wallet-api /opt/mevacoin/build/src/wallet-api --port 8070 --rpc-bind-ip 0.0.0.0 --enable-cors "*" --rpc-password "desy2011"
WorkingDirectory=/opt/mevacoin/build/src
Restart=always
RestartSec=5
User=root
LimitNOFILE=4096

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl start wallet-api
sudo systemctl enable wallet-api

echo "Imposto permessi corretti..."
sudo chown -R root:root /opt/mevacoin
sudo chmod -R 755 /opt/mevacoin
sudo mkdir -p /opt/mevacoin/logs
sudo chmod -R 775 /opt/mevacoin/logs
sudo chmod +x /opt/mevacoin/build/src/mevacoind
sudo chmod +x /opt/mevacoin/build/src/wallet-api

echo "Verifica UFW:"
sudo ufw status verbose

echo "Installazione e configurazione completata con successo."git clone https://github.com/pasqualelembo78/mevacoin.git /opt/mevacoin

# Compilazione
cd /opt/mevacoin || exit 1
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo "Configurazione del servizio mevacoind..."
sudo tee /etc/systemd/system/mevacoind.service > /dev/null <<EOF
[Unit]
Description=Mevacoin Daemon
After=network.target

[Service]
ExecStart=/opt/mevacoin/build/src/mevacoind --rpc-bind-ip 0.0.0.0
WorkingDirectory=/opt/mevacoin/build/src
Restart=always
RestartSec=5
User=root
LimitNOFILE=4096

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl start mevacoind
sudo systemctl enable mevacoind

echo "Configurazione del servizio wallet-api..."
sudo tee /etc/systemd/system/wallet-api.service > /dev/null <<EOF
[Unit]
Description=Mevacoin Wallet API
After=network.target

[Service]
# Imposta la variabile TERM prima di lanciare lo screen
Environment=TERM=xterm

# Avvia in una sessione screen staccata
ExecStart=/usr/bin/screen -DmS wallet-api /opt/mevacoin/build/src/wallet-api --port 17082 --rpc-bind-ip 0.0.0.0 --enable-cors "*" --rpc-password desy2011

# Cartella di lavoro
WorkingDirectory=/opt/mevacoin/build/src

# Mantieni il servizio attivo
Restart=always
RestartSec=5

# Utente (puoi lasciare root se stai già usando così)
User=root

# Aumenta i limiti file se serve
LimitNOFILE=4096

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl start wallet-api
sudo systemctl enable wallet-api

echo "Imposto permessi corretti..."
sudo chown -R root:root /opt/mevacoin
sudo chmod -R 755 /opt/mevacoin
sudo mkdir -p /opt/mevacoin/logs
sudo chmod -R 775 /opt/mevacoin/logs
sudo chmod +x /opt/mevacoin/build/src/mevacoind
sudo chmod +x /opt/mevacoin/build/src/wallet-api

echo "Verifica UFW:"
sudo ufw status verbose

echo "Installazione e configurazione completata con successo."
