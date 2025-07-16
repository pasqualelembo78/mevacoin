 
#!/bin/bash

# Abilita uscita immediata in caso di errore
set -e

echo "Aggiornamento del sistema..."
sudo apt update && sudo apt upgrade -y

echo "Installazione pacchetti di base e Apache..."
sudo apt install -y build-essential cmake git ccache clang g++ python3 python3-pip \
libssl-dev libzmq3-dev libsodium-dev libbz2-dev zlib1g-dev liblzma-dev \
libboost-all-dev apache2 ufw

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

# Rimuove qualsiasi cartella esistente
sudo rm -rf /opt/mevacoin

# Clona il repository
git clone https://github.com/pasqualelembo78/mevacoin.git /opt/mevacoin

# Compilazione
cd /opt/mevacoin || exit 1
mkdir build
cd build
cmake ..
make -j$(nproc)

echo "Configurazione del servizio mevacoind..."
sudo tee /etc/systemd/system/mevacoind.service > /dev/null <<EOF
[Unit]
Description=Mevacoin Daemon
After=network.target

[Service]
ExecStart=/opt/mevacoin/build/src/mevacoind
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

echo "Imposto permessi corretti..."
sudo chown -R root:root /opt/mevacoin
sudo chmod -R 755 /opt/mevacoin
sudo mkdir -p /opt/mevacoin/logs
sudo chmod -R 775 /opt/mevacoin/logs
sudo chmod +x /opt/mevacoin/build/src/mevacoind

echo "Verifica UFW:"
sudo ufw status verbose

echo "Installazione e configurazione completata con successo."
