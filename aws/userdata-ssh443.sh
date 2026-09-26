#!/bin/bash
# EC2 user data: sshd on 22 and 443, because some networks block outbound SSH on port 22.
# Ubuntu 24.04 starts sshd through ssh.socket, which ignores Port in sshd_config. Switch back to
# ssh.service (Ubuntu's documented way), then add the ports. If that fails, fall back to the socket.
# Don't restart ssh.socket with a ListenStream override instead: on this AMI that killed sshd.
printf 'Port 22\nPort 443\n' > /etc/ssh/sshd_config.d/10-ports.conf
systemctl disable --now ssh.socket
rm -f /etc/systemd/system/ssh.service.d/00-socket.conf /etc/systemd/system/ssh.socket.d/addresses.conf
systemctl daemon-reload
if ! systemctl enable --now ssh.service || ! systemctl restart ssh.service; then
  rm -f /etc/ssh/sshd_config.d/10-ports.conf
  systemctl enable --now ssh.socket
fi
