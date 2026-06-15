# Mod player demo

This demo shows video and audio capabilities on the rpi pico using the latest picoDVI with HDMI by adding a mod player and a simple visualization based on what is being played

## Processing mod files

1. Choose your favorite mod, leverage [Mod Archive](https://modarchive.org/) or [exotica](https://www.exotica.org.uk/wiki/)
2. Play the mods on your computer with xmp
3. Transform the mod into a header file with xxd -i music.mod > music_mod.h
4. Include the mod into a list of mods into the code

## Licenses and recognitions
[Micromod](https://github.com/martincameron/micromod) is a development of Martin Cameron over BSD-3-Clause license
All mod songs to their respective owners