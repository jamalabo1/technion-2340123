cp ./linux-4.15.0/Makefile ./linux-4.15.18-custom/Makefile
cd ./linux-4.15.18-custom
yes '' | make localmodconfig
yes '' | make oldconfig