cd ~/linux-4.15.18-custom/
make -j $(nproc) bzImage
sudo cp -f arch/x86/boot/bzImage /boot/vmlinuz-4.15.18-custom