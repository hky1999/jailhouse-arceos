# Generate system configuration
sudo python3 ./tools/jailhouse-config-create --mem-hv 512M ./configs/x86/qemu-ubuntu.c
sudo chown $(whoami) ./configs/x86/qemu-ubuntu.c
echo "Created config file at './configs/x86/qemu-ubuntu.c'"

# Extract kernel command line
cmdline=$(cat ./configs/x86/qemu-ubuntu.c | grep memmap | tr -d " \*" | sed 's/\$/\\\\\\\\\\$/g')

# Update grub config
cmdline=$(echo $cmdline | sed 's/\\/\\\\/g')
sudo sed -i "s/GRUB_CMDLINE_LINUX=.*/GRUB_CMDLINE_LINUX=$cmdline/" /etc/default/grub
echo "Appended kernel cmdline: $cmdline, see '/etc/default/grub'"
