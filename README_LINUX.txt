SwitchRecomp v190 para Linux (x86_64)
=====================================
Usa tu propia copia del juego. No incluye juegos ni claves.

QUE NECESITAS
  - Una distro con glibc 2.34 o mas nueva (Ubuntu 22.04+, Debian 12+, Fedora 35+, Arch, Mint 21+...).
  - Una GPU con OpenGL 4.5 (Mesa reciente, NVIDIA o AMD).
  - Python 3 (casi todas las distros ya lo traen). Si no lo tienes, Install.sh lo pide al gestor de paquetes.
  - Lo demas lo descarga solo, en la misma carpeta, sin contrasena:
      SDL2: la primera vez que abres SwitchRecomp (si tu sistema no lo tiene).
      GCC: la primera vez que recompilas un juego (si tu sistema no tiene gcc).
    Tambien puedes hacerlo antes con doble click en Install.sh (si se abre en un editor: clic derecho ->
    "Ejecutar como programa", o en una terminal:  ./SwitchRecomp/Install.sh).
  - Para elegir carpetas: zenity o kdialog (casi todas las distros ya lo traen).

COMO SE USA
  1. Descomprime:  tar xzf SwitchRecomp_v190_linux.tar.gz
  2. Abre la interfaz:  ./SwitchRecomp/SwitchRecomp  (la primera vez descarga SDL2 si hace falta)
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
  No necesita FUSE. SDL2 y GCC se descargan solos cuando hacen falta (ver arriba).

LO MISMO QUE EN WINDOWS
  Cada juego tiene su <nombre>.ini (resolucion, modo dock, idioma, controles...).
  Teclas: F1 ayuda, F3 captura, F4 limite de FPS, F5 traza, F8 captura a resolucion completa, F11 pantalla completa.
  Actualizar un juego al runtime nuevo = copiar bin/runtime_linux_x11 encima de games/<nombre>
  (la interfaz lo hace sola en "Runtime"). Partidas, opciones y codigo del juego no se tocan.

LO QUE NO HAY EN LINUX (todavia)
  - Grabacion de video con F7 (usa Media Foundation de Windows).
  - Generacion de frames LSFG si no tienes Lossless.dll.

SI LA INTERFAZ NO ABRE
  Falta SDL2: corre ./SwitchRecomp/Install.sh sdl (lo descarga en lib/). Para recompilarla desde el codigo:
    sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev && sh SwitchRecomp/tool/src/build_tool_linux.sh
  El runtime tambien se puede recompilar (necesita libx11-dev y libgl-dev):
    python3 SwitchRecomp/switchrecomp.py --build-runtime --target linux --backend x11 --cc gcc

LICENCIA
  GNU AGPL v3 (LICENSE.txt) con los terminos adicionales de NOTICE.txt: las copias y forks tienen que mantener
  la pantalla de inicio de SwitchRECOMP (logo y aviso de copyright), el credito a MZ Labs y publicar su codigo.
  Componentes de terceros que van en el paquete, cada uno con su licencia:
    SDL2, SDL2_ttf, SDL2_image (zlib): no vienen en el paquete, Install.sh los descarga en lib/ (licencias en lib/licenses/).
    GCC (GPL con la excepcion de runtime de GCC): no viene en el paquete, Install.sh lo descarga de conda-forge.
