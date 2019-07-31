Taurion - A Fully Decentralised MMO built for the Xaya platform.
================================================================

https://taurion.io

This is the Game State Processor (GSP) - it computes the game world each block and provides an rpc interface to get the state of the game each block.

To build you will need to install libxayagame - check the tutorials for building on linux and windows

Once you have installed libxayagame you can clone this repository and build:

you will need the region and obstacle layer data. Download these and put them in the `data` folder (or `mapdata` if the symlinks do not work for you)

https://xaya.io/downloads/regiondata.dat.xz

https://xaya.io/downloads/obstacledata.dat.xz

Then

```
./autogen.sh

./configure

make
```

To run this GSP (so you can access the rpc interface) you can do so like:

```
./shipsd --xaya_rpc_url="http://user:password@localhost:8396" --game_rpc_port=8200 --datadir="somepath" -alsologtostderr
```

replace user and password with your Xayad's rpcuser and rpcpassword

Xayad needs to be run with this as an option or in xaya.conf

```
zmqpubgameblocks=tcp://127.0.0.1:28332
```

If you use the Electron Wallet on windows then this is already set.


This readme will be expanded at a future time
