# CS:GO Download Fixer

Allows you to download resources (maps, sounds and textures) from community servers without hassle.
Workaround for [issue #11](https://github.com/ValveSoftware/csgo-osx-linux/issues/11) from September 2014.

### How to use:
#### Dependencies:
```
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install make cmake gcc git libcurl-dev
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
To **update**, just run `git pull` and compile again.

You can also prevent the program from writing to CS:GO's memory. This will make the very very very slim (we're talking almost non-existent) chance of getting VAC banned none.
Just pass `-nowrite` as a CLI argument.