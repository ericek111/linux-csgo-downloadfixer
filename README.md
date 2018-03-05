# CS:GO Download Fixer

Allows you to download resources (maps, sounds and textures) from community servers without hassle.

### How to use:
#### Dependencies:
```
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install make cmake gcc git
```

#### Compile:
```
git clone https://github.com/ericek111/linux-csgo-downloadfixer
cd linux-csgo-downloadfixer
cmake .
make
```

#### Run:
You must run this as root!
```
cd [install dir]
sudo ./csgo_downloadfixer
```

You can create a launcher, but you still need to log in as root.  
Use either `gksu [install dir]/csgo_downloadfixer`, or make a simple bash script, so you won't have to enter **your password** every time you launch the game:
```
#!/bin/bash
echo [user password] | sudo -S /bin/false
sudo [install dir]/csgo_downloadfixer
```
