SwitchRecomp v190 para Linux (x86_64)
=====================================
Usa tu propia copia del juego. No incluye juegos ni claves.

QUE NECESITAS
  - Una distro con glibc 2.34 o mas nueva (Ubuntu 22.04+, Debian 12+, Fedora 35+, Arch, Mint 21+...).
  - Una GPU con OpenGL 4.5 (Mesa reciente, NVIDIA o AMD).
  - Para recompilar juegos: python3 y gcc.
  - Para la interfaz: SDL2, SDL2_ttf y SDL2_image. Para elegir carpetas: zenity o kdialog.

  Ubuntu / Debian / Mint:
    sudo apt install python3 gcc libsdl2-2.0-0 libsdl2-ttf-2.0-0 libsdl2-image-2.0-0 zenity
  Fedora:
    sudo dnf install python3 gcc SDL2 SDL2_ttf SDL2_image zenity
  Arch:
    sudo pacman -S python gcc sdl2 sdl2_ttf sdl2_image zenity

COMO SE USA
  1. Descomprime:  tar xzf SwitchRecomp_v190_linux.tar.gz
  2. Abre la interfaz:  ./SwitchRecomp/SwitchRecomp
  3. "Recompile a game" -> elige la carpeta del dump (la que tiene exefs y romfs) o un .nro.
     El juego queda en SwitchRecomp/games/<nombre> (el ejecutable) + <nombre>_code.so.
  4. Juega desde la interfaz (Start) o directamente: cd games && ./<nombre>

  Sin interfaz, desde la terminal:
    python3 SwitchRecomp/switchrecomp.py /ruta/al/dump --target linux --outdir SwitchRecomp/games --cc gcc

CON EL APPIMAGE (un solo archivo)
  chmod +x SwitchRecomp-v190-x86_64.AppImage && ./SwitchRecomp-v190-x86_64.AppImage
  La primera vez se descomprime en ~/.local/share/SwitchRecomp (tarda unos segundos) y abre la interfaz desde ahi,
  porque la interfaz guarda los juegos, opciones y caches junto a si misma. Los juegos quedan en
  ~/.local/share/SwitchRecomp/games. Un AppImage nuevo se descomprime encima: cambia el programa, tus juegos y
  partidas se quedan. Para usar otra carpeta: SWITCHRECOMP_HOME=/otra/carpeta ./SwitchRecomp-v190-x86_64.AppImage
  No necesita FUSE. Las dependencias de arriba (python3, gcc, SDL2...) siguen haciendo falta.

LO MISMO QUE EN WINDOWS
  Cada juego tiene su <nombre>.ini (resolucion, modo dock, idioma, controles...).
  Teclas: F1 ayuda, F3 captura, F4 limite de FPS, F5 traza, F8 captura a resolucion completa, F11 pantalla completa.
  Actualizar un juego al runtime nuevo = copiar bin/runtime_linux_x11 encima de games/<nombre>
  (la interfaz lo hace sola en "Runtime"). Partidas, opciones y codigo del juego no se tocan.

LO QUE NO HAY EN LINUX (todavia)
  - Grabacion de video con F7 (usa Media Foundation de Windows).
  - Generacion de frames LSFG si no tienes Lossless.dll.

FLATPAK / STEAM DECK (BORRADOR)
  Hay un manifiesto inicial en org.mzlabs.SwitchRecomp.yml. La interfaz guarda por defecto los juegos en
  $XDG_DATA_HOME/SwitchRecomp/games (en Flatpak, dentro de ~/.var/app/org.mzlabs.SwitchRecomp/data),
  y los ajustes en $XDG_CONFIG_HOME/SwitchRecomp. El directorio de instalacion se mantiene solo para lectura.
  Para compilar el Flatpak: flatpak-builder --user --install --force-clean build-dir org.mzlabs.SwitchRecomp.yml
  Este manifiesto es una base de empaquetado, no un Flatpak listo para publicar. Todavia hay que integrar
  SDL2_image/SDL2_ttf, Python, gcc/clang y un selector de archivos compatible con el portal de Flatpak.

SI LA INTERFAZ NO ABRE
  Falta SDL2_ttf o SDL2_image: instala los paquetes de arriba. Para recompilarla desde el codigo:
    sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev && sh SwitchRecomp/tool/src/build_tool_linux.sh
  El runtime tambien se puede recompilar (necesita libx11-dev y libgl-dev):
    python3 SwitchRecomp/switchrecomp.py --build-runtime --target linux --backend x11 --cc gcc

LICENCIA
  GNU AGPL v3 (LICENSE.txt) con los terminos adicionales de NOTICE.txt: las copias y forks tienen que mantener
  la pantalla de inicio de SwitchRECOMP (logo y aviso de copyright), el credito a MZ Labs y publicar su codigo.
