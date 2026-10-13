#!/bin/bash

/home/user/Fuzzer/bin/qemu/bin/qemu-system-x86_64 \
	-m 2G \
	-smp 2 \
	-machine q35\
	-kernel /home/user/Fuzzer/build/linux/linux-7.1-defconfig/arch/x86/boot/bzImage \
	-append "console=ttyS0 root=/dev/vda earlyprintk=serial net.ifnames=0 panic_on_warn=0 pci=realloc selinux=0 enforcing=0" \
	-drive file=/home/user/Fuzzer/third_tool/Image-7.0/bullseye.img,format=raw,if=virtio \
	-device pcie-root-port,id=rp1 \
	-netdev user,id=net_ne2k,host=10.0.2.12,hostfwd=tcp:127.0.0.1:10022-:22 \
	-device ne2k_pci,netdev=net_ne2k \
	-net user,host=10.0.2.10,hostfwd=tcp:127.0.0.1:10021-:22 \
	-net nic,model=e1000 \
	-enable-kvm \
	-nographic \
	-serial mon:stdio \
	-fsdev local,id=fsdev0,path=/home/user/Fuzzer/out/share,security_model=none \
	-device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=hostshare \
	-pidfile vm.pid \
	-d guest_errors,unimp \
	-snapshot \
	-s \
	2>&1 | tee /home/user/Fuzzer/out/vm.log

