cmd_/home/anujajoshi/comp-blk-driver/main.mod := printf '%s\n'   main.o | awk '!x[$$0]++ { print("/home/anujajoshi/comp-blk-driver/"$$0) }' > /home/anujajoshi/comp-blk-driver/main.mod
