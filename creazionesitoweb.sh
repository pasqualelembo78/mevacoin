#!/bin/bash
set -e

DOMAIN="mevacoin.com"
WWW_DOMAIN="www.mevacoin.com"
DOC_ROOT="/var/www/mevacoin"
APACHE_CONF="/etc/apache2/sites-available/mevacoin.conf"

echo "[1/6] Aggiornamento sistema..."
sudo apt update && sudo apt upgrade -y

echo "[2/6] Installazione Apache2..."
sudo apt install -y apache2

echo "[3/6] Configurazione sito..."
# Crea cartella del sito
sudo mkdir -p $DOC_ROOT
sudo chown -R www-data:www-data $DOC_ROOT
sudo chmod -R 755 $DOC_ROOT

# Pagina test
echo "<h1>Benvenuto su Mevacoin!</h1>" | sudo tee $DOC_ROOT/index.html > /dev/null

# File di configurazione Apache
sudo bash -c "cat > $APACHE_CONF" <<EOL
<VirtualHost *:80>
    ServerAdmin webmaster@$DOMAIN
    ServerName $DOMAIN
    ServerAlias $WWW_DOMAIN
    DocumentRoot $DOC_ROOT

    <Directory $DOC_ROOT>
        Options Indexes FollowSymLinks
        AllowOverride All
        Require all granted
    </Directory>

    ErrorLog \${APACHE_LOG_DIR}/mevacoin_error.log
    CustomLog \${APACHE_LOG_DIR}/mevacoin_access.log combined
</VirtualHost>
EOL

# Abilita sito
sudo a2dissite 000-default.conf || true
sudo a2ensite mevacoin.conf
sudo systemctl reload apache2

echo "[4/6] Installazione Certbot..."
sudo apt install -y certbot python3-certbot-apache

echo "[5/6] Richiesta certificato SSL..."
sudo certbot --apache -d $DOMAIN -d $WWW_DOMAIN --non-interactive --agree-tos -m admin@$DOMAIN --redirect

echo "[6/6] Test rinnovo certificato..."
sudo certbot renew --dry-run

echo "✅ Installazione completata!"
echo "Sito disponibile su: https://$WWW_DOMAIN"
