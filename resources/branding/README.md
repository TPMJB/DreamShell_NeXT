# K-UI artwork

Approved K-UI (Katana User Interface) identity, created with image-generation
assistance for TPMJB: chrome lettering, a woman's profile with VR visor, dark
navy, electric cyan and magenta. The PNGs here are the source artwork.

`utils/build_kui_artwork.py --makeip PATH --kmgenc PATH` performs the hardware
format conversion using Pillow, CairoSVG and the KallistiOS host encoders.
The 640×480 core viewport remains inside its original 1024×512 16-bit texture.
The boot-disc badge remains inside the existing 8 KiB MR reservation. The tracked bootstrap stays unchanged; the build applies the badge to its copy.
Code outside that reservation is preserved and checked by the regression suite.
The app banner is a 256×128 texture; launcher icons remain 64×64.

Existing licenses and upstream attribution remain in NOTICE. The new name does
not change DS folders, module identifiers, settings formats or saved-rip files.
