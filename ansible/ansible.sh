#! /bin/bash
# this file is intended for manually deploy from your local computer and not in the GitHub Actions workflow

# checks if keypair doesn't exist
if [ ! -f ~/.ssh/id_mevacoin ]; then
    ssh-keygen -t rsa -N "" -f ~/.ssh/id_mevacoin
    eval "$(ssh-agent -s)" &>/dev/null
    ssh-add ~/.ssh/id_mevacoin &>/dev/null
    ssh-copy-id -i ~/.ssh/id_mevacoin.pub ubuntu@xkr.mjovanc.com
fi

ansible-playbook provision_vps.yml -i prod.inventory --ask-vault-pass
