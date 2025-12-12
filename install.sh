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
php php-cli php-curl curl screen

echo "Configurazione variabili d'ambiente Boost..."
{
  echo 'export BOOST_ROOT=/usr'
  echo 'export BOOST_INCLUDEDIR=/usr/include'
  echo 'export BOOST_LIBRARYDIR=/usr/lib/x86_64-linux-gnu'
} >> /root/.bashrc

export BOOST_ROOT=/usr
export BOOST_INCLUDEDIR=/usr/include
export BOOST_LIBRARYDIR=/usr/lib/x86_64-linux-gnu

echo "Configurazione del firewall UFW..."
sudo ufw allow 22/tcp         # SSH
sudo ufw allow 21/tcp         # FTP
sudo ufw allow 4000/tcp       # NoMachine
sudo ufw allow 17080/tcp      # Mevacoin P2P
sudo ufw allow 17081/tcp      # Mevacoin RPC
sudo ufw allow 17082/tcp      # Wallet-API (screen)
sudo ufw allow 'Apache'       # HTTP 80
sudo ufw allow 'Apache Full'  # HTTP + HTTPS
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
ExecStart=/opt/mevacoin/build/src/mevacoind --config-file /opt/mevacoin/mevacoind.conf
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

echo "Avvio del wallet-api in screen..."
cd /opt/mevacoin/build/src
export API_KEY=$(grep ^API_KEY= /opt/mevacoin_config/.env | cut -d '=' -f2-)
screen -DmS wallet-api \
  ./wallet-api \
  --port 8070 \
  --rpc-bind-ip 0.0.0.0 \
  --enable-cors "*" \
  --rpc-password "$API_KEY"
  
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
